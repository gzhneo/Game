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

////// NetCommandWrapperList.cpp ////////////////////////////////
// Bryan Cleveland

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/NetCommandWrapperList.h"
#include "GameNetwork/NetPacket.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
////// NetCommandWrapperListNode ///////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

NetCommandWrapperListNode::NetCommandWrapperListNode(NetWrapperCommandMsg *msg) 
{
	//Added By Sadullah Nader
	//Initializations inserted
	m_next = nullptr;

	//

	m_numChunks = msg->getNumChunks();
	m_chunksPresent = NEW Bool[m_numChunks];	// pool[]ify
	m_numChunksPresent = 0;

	for (Int i = 0; static_cast<UnsignedInt>(i) < m_numChunks; ++i) {
		m_chunksPresent[i] = FALSE;
	}

	m_dataLength = msg->getTotalDataLength();
	m_data = NEW UnsignedByte[m_dataLength];	// pool[]ify

	m_commandID = msg->getWrappedCommandID();
	m_playerID = msg->getPlayerID();
}

NetCommandWrapperListNode::~NetCommandWrapperListNode() {
	if (m_chunksPresent != nullptr) {
		delete[] m_chunksPresent;
		m_chunksPresent = nullptr;
	}

	if (m_data != nullptr) {
		delete[] m_data;
		m_data = nullptr;
	}
}

Bool NetCommandWrapperListNode::isComplete() {
	return m_numChunksPresent == m_numChunks;
}

Int NetCommandWrapperListNode::getPercentComplete(void) {
	if (isComplete())
		return 100;
	else
		return min(99, REAL_TO_INT( ((Real)m_numChunksPresent)/((Real)m_numChunks)*100.0f ));
}

UnsignedShort NetCommandWrapperListNode::getCommandID() {
	return m_commandID;
}

UnsignedInt NetCommandWrapperListNode::getRawDataLength() {
	return m_dataLength;
}

// Wrapped commands come in chunks from other players. Chunk count, total size, offset and length
// decide the allocation and where each chunk is copied, so they must be consistent.
static const UnsignedInt MAX_WRAPPED_COMMAND_SIZE = 8 * 1024 * 1024;
static const Int MAX_PENDING_WRAPPED_COMMANDS = 32;

static Bool isConsistentChunk(NetWrapperCommandMsg *msg, UnsignedInt numChunks, UnsignedInt totalLength)
{
	return msg->getNumChunks() == numChunks && msg->getTotalDataLength() == totalLength
		&& msg->getChunkNumber() < numChunks
		&& static_cast<unsigned long long>(msg->getDataOffset()) + msg->getDataLength() <= totalLength;
}

void NetCommandWrapperListNode::copyChunkData(NetWrapperCommandMsg *msg) {
	if (msg == nullptr) {
		DEBUG_CRASH(("Trying to copy data from a non-existent wrapper command message"));
		return;
	}

	DEBUG_ASSERTCRASH(msg->getChunkNumber() < m_numChunks, ("MunkeeChunk %d of %d\n",
		msg->getChunkNumber(), m_numChunks));
	if (msg->getChunkNumber() >= m_numChunks)
		return;

	DEBUG_LOG(("NetCommandWrapperListNode::copyChunkData() - copying chunk %d\n",
		msg->getChunkNumber()));

	if (m_chunksPresent[msg->getChunkNumber()] == TRUE) {
		// we already received this chunk, no need to recopy it.
		return;
	}

	m_chunksPresent[msg->getChunkNumber()] = TRUE;
	if (!isConsistentChunk(msg, m_numChunks, m_dataLength)) {
		DEBUG_LOG(("NetCommandWrapperListNode::copyChunkData() - inconsistent chunk dropped\n"));
		return;
	}

	UnsignedInt offset = msg->getDataOffset();
	memcpy(m_data + offset, msg->getData(), msg->getDataLength());
	++m_numChunksPresent;
}

UnsignedByte * NetCommandWrapperListNode::getRawData() {
	return m_data;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////// NetCommandWrapperList ///////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

NetCommandWrapperList::NetCommandWrapperList() {
	m_list = nullptr;
}

NetCommandWrapperList::~NetCommandWrapperList() {
	NetCommandWrapperListNode *temp;
	while (m_list != nullptr) {
		temp = m_list->m_next;
		deleteInstanceOf(m_list);
		m_list = temp;
	}
}

void NetCommandWrapperList::init() {
	m_list = nullptr;
}

void NetCommandWrapperList::reset() {
	NetCommandWrapperListNode *temp;
	while (m_list != nullptr) {
		temp = m_list->m_next;
		deleteInstanceOf(m_list);
		m_list = temp;
	}
}

Int NetCommandWrapperList::getPercentComplete(UnsignedShort wrappedCommandID)
{
	NetCommandWrapperListNode *temp = m_list;

	while ((temp != nullptr) && (temp->getCommandID() != wrappedCommandID)) {
		temp = temp->m_next;
	}

	if (!temp)
		return 0;

	return temp->getPercentComplete();
}

void NetCommandWrapperList::processWrapper(NetCommandRef *ref) {
	NetCommandWrapperListNode *temp = m_list;
	NetWrapperCommandMsg *msg = static_cast<NetWrapperCommandMsg *>(ref->getCommand());

	// command IDs are counted per player: the chunks of two players' commands must not mix
	while ((temp != nullptr) && (temp->getCommandID() != msg->getWrappedCommandID() || temp->getPlayerID() != msg->getPlayerID())) {
		temp = temp->m_next;
	}

	if (temp == nullptr) {
		Int pending = 0;
		for (NetCommandWrapperListNode *n = m_list; n != nullptr; n = n->m_next)
			++pending;
		if (pending >= MAX_PENDING_WRAPPED_COMMANDS) {
			DEBUG_LOG(("NetCommandWrapperList::processWrapper() - too many incomplete wrapped commands, dropped\n"));
			return;
		}
		const UnsignedInt numChunks = msg->getNumChunks();
		const UnsignedInt totalLength = msg->getTotalDataLength();
		if (numChunks == 0 || totalLength == 0 || totalLength > MAX_WRAPPED_COMMAND_SIZE || numChunks > totalLength
			|| !isConsistentChunk(msg, numChunks, totalLength)) {
			DEBUG_LOG(("NetCommandWrapperList::processWrapper() - wrapper with %u chunks / %u bytes dropped\n", numChunks, totalLength));
			return;
		}
		temp = newInstance(NetCommandWrapperListNode)(msg);
		temp->m_next = m_list;
		m_list = temp;
	}

	temp->copyChunkData(msg);
}

NetCommandList * NetCommandWrapperList::getReadyCommands() 
{
	NetCommandList *retlist = newInstance(NetCommandList);
	retlist->init();

	NetCommandWrapperListNode *temp = m_list;
	NetCommandWrapperListNode *next = nullptr;

	while (temp != nullptr) {
		next = temp->m_next;
		if (temp->isComplete()) {
			NetCommandRef *msg = NetPacket::ConstructNetCommandMsgFromRawData(temp->getRawData(), temp->getRawDataLength());
			// ConstructNetCommandMsgFromRawData returns NULL for damaged or truncated packets. The
			// wrapped command must be one of the player who sent the chunks.
			if (msg != nullptr && msg->getCommand()->getPlayerID() != temp->getPlayerID()) {
				DEBUG_LOG(("NetCommandWrapperList::getReadyCommands() - wrapped command of player %d in chunks of player %d dropped\n",
					(int)msg->getCommand()->getPlayerID(), (int)temp->getPlayerID()));
				deleteInstanceOf(msg);
				msg = nullptr;
			}
			if (msg != nullptr) {
				NetCommandRef *ret = retlist->addMessage(msg->getCommand());
				ret->setRelay(msg->getRelay());

				deleteInstanceOf(msg);
				msg = nullptr;
			}

			removeFromList(temp);
			temp = nullptr;
		}
		temp = next;
	}

	return retlist;
}

void NetCommandWrapperList::removeFromList(NetCommandWrapperListNode *node) {
	if (node == nullptr) {
		return;
	}

	NetCommandWrapperListNode *temp = m_list;
	NetCommandWrapperListNode *prev = nullptr;

	while ((temp != nullptr) && (temp->getCommandID() != node->getCommandID())) {
		prev = temp;
		temp = temp->m_next;
	}

	if (temp == nullptr) {
		return;
	}

	if (prev == nullptr) {
		m_list = temp->m_next;
		deleteInstanceOf(temp);
		temp = nullptr;
	} else {
		prev->m_next = temp->m_next;
		deleteInstanceOf(temp);
		temp = nullptr;
	}
}
