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

////////// NetPacket.cpp ///////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include <memory>

#include "GameNetwork/NetPacket.h"
#include "GameNetwork/NetCommandMsg.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/networkutil.h"
#include "GameNetwork/GameMessageParser.h"

/*
Wire format. A packet is a sequence of tagged fields:
	T  command type        1 byte
	F  execution frame     4 bytes
	R  relay mask          1 byte
	P  player ID           1 byte
	C  command ID          2 bytes
	D  command data        layout depends on the command type
	Z  repeat              the previous ack or frame command, advanced by one
A field keeps its value until it is sent again, so a command in a packet carries only the header
fields that differ from the previous command; a new player also restates the command ID, and the
command ID otherwise counts up by one per command that has one. A command outside a packet (the
data of a wrapper command) carries all of its header fields.
*/

namespace {

// Collects bytes into a buffer, or only counts them when there is no buffer. Bytes beyond the
// capacity are counted but not stored.
class PayloadWriter
{
public:
	PayloadWriter(UnsignedByte *buffer, std::size_t capacity) : m_buffer(buffer), m_capacity(capacity) {}

	void put(const void *data, std::size_t size)
	{
		if (m_buffer != nullptr && size <= m_capacity && m_length <= m_capacity - size) {
			memcpy(m_buffer + m_length, data, size);
		}
		m_length += size;
	}

	template <typename T>
	void put(T value) { put(&value, sizeof(value)); }

	UnsignedInt length() const { return static_cast<UnsignedInt>(m_length); }

private:
	UnsignedByte *m_buffer;
	std::size_t m_capacity;
	std::size_t m_length = 0;
};

// Reads fixed-size fields; every read is checked against the received length.
class PacketReader
{
public:
	PacketReader(UnsignedByte *data, Int &i, UnsignedInt dataLength) : m_data(data), m_i(i), m_length(dataLength) {}

	template <typename T>
	Bool get(T &value)
	{
		if (!has(sizeof(T))) {
			return FALSE;
		}
		memcpy(&value, m_data + m_i, sizeof(T));
		m_i += static_cast<Int>(sizeof(T));
		return TRUE;
	}

	Bool has(std::size_t size) const { return size <= 0xFFFFFFFFu && NetPacket::PacketHasBytes(m_i, static_cast<UnsignedInt>(size), m_length); }
	UnsignedByte *here() const { return m_data + m_i; }
	void skip(std::size_t size) { m_i += static_cast<Int>(size); }

	// Reads a NUL-terminated name of at most _MAX_PATH-1 characters.
	Bool getName(char (&name)[_MAX_PATH])
	{
		Int len = 0;
		while (m_i < static_cast<Int>(m_length) && m_data[m_i] != 0) {
			if (len >= _MAX_PATH - 1) {
				return FALSE;
			}
			name[len++] = m_data[m_i++];
		}
		if (m_i >= static_cast<Int>(m_length)) {
			return FALSE;
		}
		name[len] = 0;
		++m_i;
		return TRUE;
	}

private:
	UnsignedByte *m_data;
	Int &m_i;
	UnsignedInt m_length;
};

NetCommandMsg *dropCommand(NetCommandMsg *msg, const char *what)
{
	DEBUG_LOG(("NetPacket - %s command damaged or cut off, dropped\n", what));
	deleteInstanceOf(msg);
	return nullptr;
}

// --- game command ---------------------------------------------------------------------------------

void writeGameArgument(PayloadWriter &w, GameMessageArgumentDataType type, const GameMessageArgumentType &arg)
{
	switch (type) {
		case ARGUMENTDATATYPE_INTEGER:     w.put(arg.integer); break;
		case ARGUMENTDATATYPE_REAL:        w.put(arg.real); break;
		case ARGUMENTDATATYPE_BOOLEAN:     w.put(arg.boolean); break;
		case ARGUMENTDATATYPE_OBJECTID:    w.put(arg.objectID); break;
		case ARGUMENTDATATYPE_DRAWABLEID:  w.put(arg.drawableID); break;
		case ARGUMENTDATATYPE_TEAMID:      w.put(arg.teamID); break;
		case ARGUMENTDATATYPE_LOCATION:    w.put(arg.location); break;
		case ARGUMENTDATATYPE_PIXEL:       w.put(arg.pixel); break;
		case ARGUMENTDATATYPE_PIXELREGION: w.put(arg.pixelRegion); break;
		case ARGUMENTDATATYPE_TIMESTAMP:   w.put(arg.timestamp); break;
		case ARGUMENTDATATYPE_WIDECHAR:    w.put(arg.wChar); break;
		default: break;
	}
}

// Game message type, the argument types with their counts, then the arguments.
void writeGamePayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	GameMessage *gmsg = static_cast<NetGameCommandMsg *>(cmd)->constructGameMessage();
	w.put(gmsg->getType());

	GameMessageParser *parser = newInstance(GameMessageParser)(gmsg);
	w.put(static_cast<UnsignedByte>(parser->getNumTypes()));
	for (GameMessageParserArgumentType *argType = parser->getFirstArgumentType(); argType != nullptr; argType = argType->getNext()) {
		w.put(static_cast<UnsignedByte>(argType->getType()));
		w.put(static_cast<UnsignedByte>(argType->getArgCount()));
	}
	deleteInstanceOf(parser);

	const Int numArgs = gmsg->getArgumentCount();
	for (Int i = 0; i < numArgs; ++i) {
		writeGameArgument(w, gmsg->getArgumentDataType(i), *(gmsg->getArgument(i)));
	}
	deleteInstanceOf(gmsg);
}

// An argument of an unknown type is skipped without reading.
Bool readGameArgument(PacketReader &r, GameMessageArgumentDataType type, NetGameCommandMsg *msg)
{
	GameMessageArgumentType arg;
	Bool ok = TRUE;
	switch (type) {
		case ARGUMENTDATATYPE_INTEGER:     ok = r.get(arg.integer); break;
		case ARGUMENTDATATYPE_REAL:        ok = r.get(arg.real); break;
		case ARGUMENTDATATYPE_BOOLEAN:     ok = r.get(arg.boolean); break;
		case ARGUMENTDATATYPE_OBJECTID:    ok = r.get(arg.objectID); break;
		case ARGUMENTDATATYPE_DRAWABLEID:  ok = r.get(arg.drawableID); break;
		case ARGUMENTDATATYPE_TEAMID:      ok = r.get(arg.teamID); break;
		case ARGUMENTDATATYPE_LOCATION:    ok = r.get(arg.location); break;
		case ARGUMENTDATATYPE_PIXEL:       ok = r.get(arg.pixel); break;
		case ARGUMENTDATATYPE_PIXELREGION: ok = r.get(arg.pixelRegion); break;
		case ARGUMENTDATATYPE_TIMESTAMP:   ok = r.get(arg.timestamp); break;
		case ARGUMENTDATATYPE_WIDECHAR:    ok = r.get(arg.wChar); break;
		default: return TRUE;
	}
	if (ok) {
		msg->addArgument(type, arg);
	}
	return ok;
}

// The type table comes from the network: up to 255 types with up to 255 arguments each, all reads
// checked. A type with a count of 0 never hands over to the next type, so the remaining arguments
// are read with it.
NetCommandMsg *readGamePayload(PacketReader &r)
{
	NetGameCommandMsg *msg = newInstance(NetGameCommandMsg);

	GameMessage::Type gameType;
	UnsignedByte numArgTypes = 0;
	if (!r.get(gameType) || !r.get(numArgTypes)) {
		return dropCommand(msg, "game");
	}
	msg->setGameMessageType(gameType);

	Int totalArgCount = 0;
	GameMessageParser *parser = newInstance(GameMessageParser)();
	for (Int j = 0; j < numArgTypes; ++j) {
		UnsignedByte type = static_cast<UnsignedByte>(ARGUMENTDATATYPE_UNKNOWN);
		UnsignedByte argCount = 0;
		if (!r.get(type) || !r.get(argCount)) {
			deleteInstanceOf(parser);
			return dropCommand(msg, "game");
		}
		parser->addArgType(static_cast<GameMessageArgumentDataType>(type), argCount);
		totalArgCount += argCount;
	}

	GameMessageParserArgumentType *argType = parser->getFirstArgumentType();
	GameMessageArgumentDataType type = ARGUMENTDATATYPE_UNKNOWN;
	Int argsLeftForType = 0;
	if (argType != nullptr) {
		type = argType->getType();
		argsLeftForType = argType->getArgCount();
	}
	for (Int j = 0; j < totalArgCount; ++j) {
		if (!readGameArgument(r, type, msg)) {
			deleteInstanceOf(parser);
			return dropCommand(msg, "game");
		}
		if (--argsLeftForType == 0 && argType != nullptr) {
			argType = argType->getNext();
			if (argType != nullptr) {
				argsLeftForType = argType->getArgCount();
				type = argType->getType();
			}
		}
	}
	deleteInstanceOf(parser);
	return msg;
}

// --- the other command types ----------------------------------------------------------------------

template <typename AckMsg>
void writeAckFields(PayloadWriter &w, NetCommandMsg *cmd)
{
	AckMsg *ack = static_cast<AckMsg *>(cmd);
	w.put(ack->getCommandID());
	w.put(ack->getOriginalPlayerID());
}

void writeAckPayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	switch (cmd->getNetCommandType()) {
		case NETCOMMANDTYPE_ACKBOTH:   writeAckFields<NetAckBothCommandMsg>(w, cmd); break;
		case NETCOMMANDTYPE_ACKSTAGE1: writeAckFields<NetAckStage1CommandMsg>(w, cmd); break;
		default:                       writeAckFields<NetAckStage2CommandMsg>(w, cmd); break;
	}
}

template <typename AckMsg>
NetCommandMsg *readAckFields(PacketReader &r, AckMsg *msg)
{
	UnsignedShort commandID = 0;
	UnsignedByte originalPlayerID = 0;
	if (!r.get(commandID) || !r.get(originalPlayerID)) {
		return dropCommand(msg, "ack");
	}
	msg->setCommandID(commandID);
	msg->setOriginalPlayerID(originalPlayerID);
	return msg;
}

NetCommandMsg *readAckBothPayload(PacketReader &r) { return readAckFields(r, newInstance(NetAckBothCommandMsg)); }
NetCommandMsg *readAckStage1Payload(PacketReader &r) { return readAckFields(r, newInstance(NetAckStage1CommandMsg)); }
NetCommandMsg *readAckStage2Payload(PacketReader &r) { return readAckFields(r, newInstance(NetAckStage2CommandMsg)); }

void writeFramePayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	w.put(static_cast<NetFrameCommandMsg *>(cmd)->getCommandCount());
}

NetCommandMsg *readFramePayload(PacketReader &r)
{
	NetFrameCommandMsg *msg = newInstance(NetFrameCommandMsg);
	UnsignedShort commandCount = 0;
	if (!r.get(commandCount)) {
		return dropCommand(msg, "frame");
	}
	msg->setCommandCount(commandCount);
	return msg;
}

void writePlayerLeavePayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	w.put(static_cast<NetPlayerLeaveCommandMsg *>(cmd)->getLeavingPlayerID());
}

NetCommandMsg *readPlayerLeavePayload(PacketReader &r)
{
	NetPlayerLeaveCommandMsg *msg = newInstance(NetPlayerLeaveCommandMsg);
	UnsignedByte leavingPlayerID = 0;
	if (!r.get(leavingPlayerID)) {
		return dropCommand(msg, "player leave");
	}
	msg->setLeavingPlayerID(leavingPlayerID);
	return msg;
}

void writeRunAheadMetricsPayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	NetRunAheadMetricsCommandMsg *msg = static_cast<NetRunAheadMetricsCommandMsg *>(cmd);
	w.put(msg->getAverageLatency());
	w.put(static_cast<UnsignedShort>(msg->getAverageFps()));
}

NetCommandMsg *readRunAheadMetricsPayload(PacketReader &r)
{
	NetRunAheadMetricsCommandMsg *msg = newInstance(NetRunAheadMetricsCommandMsg);
	Real averageLatency = 0;
	UnsignedShort averageFps = 0;
	if (!r.get(averageLatency) || !r.get(averageFps)) {
		return dropCommand(msg, "run ahead metrics");
	}
	msg->setAverageLatency(averageLatency);
	msg->setAverageFps(static_cast<Int>(averageFps));
	return msg;
}

void writeRunAheadPayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	NetRunAheadCommandMsg *msg = static_cast<NetRunAheadCommandMsg *>(cmd);
	w.put(msg->getRunAhead());
	w.put(msg->getFrameRate());
}

NetCommandMsg *readRunAheadPayload(PacketReader &r)
{
	NetRunAheadCommandMsg *msg = newInstance(NetRunAheadCommandMsg);
	UnsignedShort runAhead = 0;
	UnsignedByte frameRate = 0;
	if (!r.get(runAhead) || !r.get(frameRate)) {
		return dropCommand(msg, "run ahead");
	}
	msg->setRunAhead(runAhead);
	msg->setFrameRate(frameRate);
	return msg;
}

void writeDestroyPlayerPayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	w.put(static_cast<NetDestroyPlayerCommandMsg *>(cmd)->getPlayerIndex());
}

NetCommandMsg *readDestroyPlayerPayload(PacketReader &r)
{
	NetDestroyPlayerCommandMsg *msg = newInstance(NetDestroyPlayerCommandMsg);
	UnsignedInt playerIndex = 0;
	if (!r.get(playerIndex)) {
		return dropCommand(msg, "destroy player");
	}
	msg->setPlayerIndex(playerIndex);
	return msg;
}

void writeNoPayload(PayloadWriter &, NetCommandMsg *) {}

NetCommandMsg *readKeepAlivePayload(PacketReader &) { return newInstance(NetKeepAliveCommandMsg); }
NetCommandMsg *readDisconnectKeepAlivePayload(PacketReader &) { return newInstance(NetDisconnectKeepAliveCommandMsg); }
NetCommandMsg *readPacketRouterQueryPayload(PacketReader &) { return newInstance(NetPacketRouterQueryCommandMsg); }
NetCommandMsg *readPacketRouterAckPayload(PacketReader &) { return newInstance(NetPacketRouterAckCommandMsg); }
NetCommandMsg *readPlainPayload(PacketReader &) { return newInstance(NetCommandMsg); }

void writeDisconnectPlayerPayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	NetDisconnectPlayerCommandMsg *msg = static_cast<NetDisconnectPlayerCommandMsg *>(cmd);
	w.put(msg->getDisconnectSlot());
	w.put(msg->getDisconnectFrame());
}

NetCommandMsg *readDisconnectPlayerPayload(PacketReader &r)
{
	NetDisconnectPlayerCommandMsg *msg = newInstance(NetDisconnectPlayerCommandMsg);
	UnsignedByte slot = 0;
	UnsignedInt disconnectFrame = 0;
	if (!r.get(slot) || !r.get(disconnectFrame)) {
		return dropCommand(msg, "disconnect player");
	}
	msg->setDisconnectSlot(slot);
	msg->setDisconnectFrame(disconnectFrame);
	return msg;
}

// Chat text: a length byte, then that many UTF-16 characters. Longer texts are cut to 255.
void writeText(PayloadWriter &w, const UnicodeString &text)
{
	const UnsignedByte length = static_cast<UnsignedByte>(text.getLength());
	w.put(length);
	w.put(text.str(), length * sizeof(UnsignedShort));
}

Bool readText(PacketReader &r, UnicodeString &text)
{
	UnsignedByte length = 0;
	if (!r.get(length) || !r.has(static_cast<UnsignedInt>(length) * sizeof(UnsignedShort))) {
		return FALSE;
	}
	UnsignedShort chars[256];
	memcpy(chars, r.here(), length * sizeof(UnsignedShort));
	r.skip(length * sizeof(UnsignedShort));
	chars[length] = 0;
	text.set(reinterpret_cast<const WideChar *>(chars));
	return TRUE;
}

void writeDisconnectChatPayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	writeText(w, static_cast<NetDisconnectChatCommandMsg *>(cmd)->getText());
}

NetCommandMsg *readDisconnectChatPayload(PacketReader &r)
{
	NetDisconnectChatCommandMsg *msg = newInstance(NetDisconnectChatCommandMsg);
	UnicodeString text;
	if (!readText(r, text)) {
		return dropCommand(msg, "disconnect chat");
	}
	msg->setText(text);
	return msg;
}

void writeChatPayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	NetChatCommandMsg *msg = static_cast<NetChatCommandMsg *>(cmd);
	writeText(w, msg->getText());
	w.put(static_cast<Int>(msg->getPlayerMask()));
}

NetCommandMsg *readChatPayload(PacketReader &r)
{
	NetChatCommandMsg *msg = newInstance(NetChatCommandMsg);
	UnicodeString text;
	Int playerMask = 0;
	if (!readText(r, text) || !r.get(playerMask)) {
		return dropCommand(msg, "chat");
	}
	msg->setText(text);
	msg->setPlayerMask(playerMask);
	return msg;
}

void writeDisconnectVotePayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	NetDisconnectVoteCommandMsg *msg = static_cast<NetDisconnectVoteCommandMsg *>(cmd);
	w.put(msg->getSlot());
	w.put(msg->getVoteFrame());
}

NetCommandMsg *readDisconnectVotePayload(PacketReader &r)
{
	NetDisconnectVoteCommandMsg *msg = newInstance(NetDisconnectVoteCommandMsg);
	UnsignedByte slot = 0;
	UnsignedInt voteFrame = 0;
	if (!r.get(slot) || !r.get(voteFrame)) {
		return dropCommand(msg, "disconnect vote");
	}
	msg->setSlot(slot);
	msg->setVoteFrame(voteFrame);
	return msg;
}

void writeRemovePlayerVotePayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	w.put(static_cast<NetRemovePlayerVoteCommandMsg *>(cmd)->getSlot());
}

NetCommandMsg *readRemovePlayerVotePayload(PacketReader &r)
{
	NetRemovePlayerVoteCommandMsg *msg = newInstance(NetRemovePlayerVoteCommandMsg);
	UnsignedByte slot = 0;
	if (!r.get(slot)) {
		return dropCommand(msg, "remove player vote");
	}
	msg->setSlot(slot);
	return msg;
}

void writePlayerReturnPayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	NetPlayerReturnCommandMsg *msg = static_cast<NetPlayerReturnCommandMsg *>(cmd);
	w.put(msg->getSlot());
	w.put(msg->getReturnFrame());
	w.put(msg->getPort());
	w.put(msg->getKey(), NetPlayerReturnCommandMsg::KEY_SIZE);
	w.put(msg->getEndpointFamily());
	w.put(msg->getEndpointAddress(), 16);
	w.put(msg->getEndpointPort());
}

NetCommandMsg *readPlayerReturnPayload(PacketReader &r)
{
	NetPlayerReturnCommandMsg *msg = newInstance(NetPlayerReturnCommandMsg);
	UnsignedByte slot = 0;
	UnsignedInt returnFrame = 0;
	UnsignedShort port = 0;
	UnsignedByte key[NetPlayerReturnCommandMsg::KEY_SIZE];
	UnsignedByte family = 0;
	UnsignedByte address[16];
	UnsignedShort endpointPort = 0;
	if (!r.get(slot) || !r.get(returnFrame) || !r.get(port) || !r.get(key) || !r.get(family) || !r.get(address)
		|| !r.get(endpointPort)) {
		return dropCommand(msg, "player return");
	}
	msg->setSlot(slot);
	msg->setReturnFrame(returnFrame);
	msg->setPort(port);
	msg->setKey(key);
	msg->setEndpoint(family, address, endpointPort);
	return msg;
}

void writeProgressPayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	w.put(static_cast<NetProgressCommandMsg *>(cmd)->getPercentage());
}

NetCommandMsg *readProgressPayload(PacketReader &r)
{
	NetProgressCommandMsg *msg = newInstance(NetProgressCommandMsg);
	UnsignedByte percentage = 0;
	if (!r.get(percentage)) {
		return dropCommand(msg, "progress");
	}
	msg->setPercentage(percentage);
	return msg;
}

void writeWrapperPayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	NetWrapperCommandMsg *msg = static_cast<NetWrapperCommandMsg *>(cmd);
	w.put(msg->getWrappedCommandID());
	w.put(msg->getChunkNumber());
	w.put(msg->getNumChunks());
	w.put(msg->getTotalDataLength());
	w.put(msg->getDataLength());
	w.put(msg->getDataOffset());
	w.put(msg->getData(), msg->getDataLength());
}

// The chunk length comes from the network and is checked against the rest of the packet.
NetCommandMsg *readWrapperPayload(PacketReader &r)
{
	NetWrapperCommandMsg *msg = newInstance(NetWrapperCommandMsg);
	UnsignedShort wrappedCommandID = 0;
	UnsignedInt chunkNumber = 0, numChunks = 0, totalDataLength = 0, chunkDataLength = 0, dataOffset = 0;
	if (!r.get(wrappedCommandID) || !r.get(chunkNumber) || !r.get(numChunks) || !r.get(totalDataLength)
			|| !r.get(chunkDataLength) || !r.get(dataOffset) || !r.has(chunkDataLength)) {
		return dropCommand(msg, "wrapper");
	}
	msg->setWrappedCommandID(wrappedCommandID);
	msg->setChunkNumber(chunkNumber);
	msg->setNumChunks(numChunks);
	msg->setTotalDataLength(totalDataLength);
	msg->setDataOffset(dataOffset);
	msg->setData(r.here(), chunkDataLength);
	r.skip(chunkDataLength);
	return msg;
}

// File names travel in portable form, NUL-terminated.
void writeName(PayloadWriter &w, const AsciiString &name)
{
	w.put(name.str(), name.getLength() + 1);
}

void writeFilePayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	NetFileCommandMsg *msg = static_cast<NetFileCommandMsg *>(cmd);
	writeName(w, msg->getPortableFilename());
	w.put(msg->getFileLength());
	w.put(msg->getFileData(), msg->getFileLength());
}

// The data length comes from the network and is checked against the rest of the packet.
NetCommandMsg *readFilePayload(PacketReader &r)
{
	NetFileCommandMsg *msg = newInstance(NetFileCommandMsg);
	char filename[_MAX_PATH];
	UnsignedInt fileLength = 0;
	if (!r.getName(filename) || !r.get(fileLength) || !r.has(fileLength)) {
		return dropCommand(msg, "file");
	}
	msg->setPortableFilename(AsciiString(filename));
	msg->setFileData(r.here(), fileLength);	// copies the data
	r.skip(fileLength);
	return msg;
}

void writeFileAnnouncePayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	NetFileAnnounceCommandMsg *msg = static_cast<NetFileAnnounceCommandMsg *>(cmd);
	writeName(w, msg->getPortableFilename());
	w.put(msg->getFileID());
	w.put(msg->getPlayerMask());
}

NetCommandMsg *readFileAnnouncePayload(PacketReader &r)
{
	NetFileAnnounceCommandMsg *msg = newInstance(NetFileAnnounceCommandMsg);
	char filename[_MAX_PATH];
	UnsignedShort fileID = 0;
	UnsignedByte playerMask = 0;
	if (!r.getName(filename) || !r.get(fileID) || !r.get(playerMask)) {
		return dropCommand(msg, "file announce");
	}
	msg->setPortableFilename(AsciiString(filename));
	msg->setFileID(fileID);
	msg->setPlayerMask(playerMask);
	return msg;
}

void writeFileProgressPayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	NetFileProgressCommandMsg *msg = static_cast<NetFileProgressCommandMsg *>(cmd);
	w.put(msg->getFileID());
	w.put(msg->getProgress());
}

NetCommandMsg *readFileProgressPayload(PacketReader &r)
{
	NetFileProgressCommandMsg *msg = newInstance(NetFileProgressCommandMsg);
	UnsignedShort fileID = 0;
	Int progress = 0;
	if (!r.get(fileID) || !r.get(progress)) {
		return dropCommand(msg, "file progress");
	}
	msg->setFileID(fileID);
	msg->setProgress(progress);
	return msg;
}

void writeDisconnectFramePayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	w.put(static_cast<NetDisconnectFrameCommandMsg *>(cmd)->getDisconnectFrame());
}

NetCommandMsg *readDisconnectFramePayload(PacketReader &r)
{
	NetDisconnectFrameCommandMsg *msg = newInstance(NetDisconnectFrameCommandMsg);
	UnsignedInt disconnectFrame = 0;
	if (!r.get(disconnectFrame)) {
		return dropCommand(msg, "disconnect frame");
	}
	msg->setDisconnectFrame(disconnectFrame);
	return msg;
}

void writeDisconnectScreenOffPayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	w.put(static_cast<NetDisconnectScreenOffCommandMsg *>(cmd)->getNewFrame());
}

NetCommandMsg *readDisconnectScreenOffPayload(PacketReader &r)
{
	NetDisconnectScreenOffCommandMsg *msg = newInstance(NetDisconnectScreenOffCommandMsg);
	UnsignedInt newFrame = 0;
	if (!r.get(newFrame)) {
		return dropCommand(msg, "disconnect screen off");
	}
	msg->setNewFrame(newFrame);
	return msg;
}

void writeFrameResendRequestPayload(PayloadWriter &w, NetCommandMsg *cmd)
{
	w.put(static_cast<NetFrameResendRequestCommandMsg *>(cmd)->getFrameToResend());
}

NetCommandMsg *readFrameResendRequestPayload(PacketReader &r)
{
	NetFrameResendRequestCommandMsg *msg = newInstance(NetFrameResendRequestCommandMsg);
	UnsignedInt frameToResend = 0;
	if (!r.get(frameToResend)) {
		return dropCommand(msg, "frame resend request");
	}
	msg->setFrameToResend(frameToResend);
	return msg;
}

// --- layout table ---------------------------------------------------------------------------------

struct CommandLayout
{
	NetCommandType type;
	// Header fields in wire order (letters as in the format description above): packedHeader for a
	// command in a packet, fullHeader for a command outside one. They differ for a few types because
	// the original protocol wrote them that way.
	const char *packedHeader;
	const char *fullHeader;
	void (*writePayload)(PayloadWriter &w, NetCommandMsg *msg);
	NetCommandMsg *(*readPayload)(PacketReader &r);
};

const CommandLayout s_layouts[] =
{
	{ NETCOMMANDTYPE_ACKBOTH,             "TP",    "TP",    writeAckPayload,                  readAckBothPayload },
	{ NETCOMMANDTYPE_ACKSTAGE1,           "TP",    "TP",    writeAckPayload,                  readAckStage1Payload },
	{ NETCOMMANDTYPE_ACKSTAGE2,           "TP",    "TP",    writeAckPayload,                  readAckStage2Payload },
	{ NETCOMMANDTYPE_FRAMEINFO,           "TFRPC", "TFRPC", writeFramePayload,                readFramePayload },
	{ NETCOMMANDTYPE_GAMECOMMAND,         "TFRPC", "TFRPC", writeGamePayload,                 readGamePayload },
	{ NETCOMMANDTYPE_PLAYERLEAVE,         "TRFPC", "TRFPC", writePlayerLeavePayload,          readPlayerLeavePayload },
	{ NETCOMMANDTYPE_RUNAHEADMETRICS,     "TRPC",  "TRPC",  writeRunAheadMetricsPayload,      readRunAheadMetricsPayload },
	{ NETCOMMANDTYPE_RUNAHEAD,            "TRFPC", "TRFPC", writeRunAheadPayload,             readRunAheadPayload },
	{ NETCOMMANDTYPE_DESTROYPLAYER,       "TRFPC", "TRFPC", writeDestroyPlayerPayload,        readDestroyPlayerPayload },
	{ NETCOMMANDTYPE_KEEPALIVE,           "TRP",   "TRP",   writeNoPayload,                   readKeepAlivePayload },
	{ NETCOMMANDTYPE_DISCONNECTCHAT,      "TRP",   "TRP",   writeDisconnectChatPayload,       readDisconnectChatPayload },
	{ NETCOMMANDTYPE_CHAT,                "TFRPC", "TFRPC", writeChatPayload,                 readChatPayload },
	{ NETCOMMANDTYPE_PROGRESS,            "TRP",   "TRP",   writeProgressPayload,             readProgressPayload },
	{ NETCOMMANDTYPE_LOADCOMPLETE,        "TRPC",  "TRPC",  writeNoPayload,                   readPlainPayload },
	{ NETCOMMANDTYPE_TIMEOUTSTART,        "TRPC",  "TRPC",  writeNoPayload,                   readPlainPayload },
	{ NETCOMMANDTYPE_WRAPPER,             "TRPC",  "TRPC",  writeWrapperPayload,              readWrapperPayload },
	{ NETCOMMANDTYPE_FILE,                "TRPC",  "TRPC",  writeFilePayload,                 readFilePayload },
	{ NETCOMMANDTYPE_FILEANNOUNCE,        "TRPC",  "TRPC",  writeFileAnnouncePayload,         readFileAnnouncePayload },
	{ NETCOMMANDTYPE_FILEPROGRESS,        "TRPC",  "TRPC",  writeFileProgressPayload,         readFileProgressPayload },
	{ NETCOMMANDTYPE_FRAMERESENDREQUEST,  "TFRPC", "TRPC",  writeFrameResendRequestPayload,   readFrameResendRequestPayload },
	{ NETCOMMANDTYPE_DISCONNECTKEEPALIVE, "TRP",   "TRP",   writeNoPayload,                   readDisconnectKeepAlivePayload },
	{ NETCOMMANDTYPE_DISCONNECTPLAYER,    "TRPC",  "TRPC",  writeDisconnectPlayerPayload,     readDisconnectPlayerPayload },
	{ NETCOMMANDTYPE_PACKETROUTERQUERY,   "TRP",   "TRP",   writeNoPayload,                   readPacketRouterQueryPayload },
	{ NETCOMMANDTYPE_PACKETROUTERACK,     "TRP",   "TRP",   writeNoPayload,                   readPacketRouterAckPayload },
	{ NETCOMMANDTYPE_DISCONNECTVOTE,      "TRPC",  "TRPC",  writeDisconnectVotePayload,       readDisconnectVotePayload },
	{ NETCOMMANDTYPE_DISCONNECTFRAME,     "TFRPC", "TRPC",  writeDisconnectFramePayload,      readDisconnectFramePayload },
	{ NETCOMMANDTYPE_DISCONNECTSCREENOFF, "TFRPC", "TRPC",  writeDisconnectScreenOffPayload,  readDisconnectScreenOffPayload },
	{ NETCOMMANDTYPE_REMOVEPLAYERVOTE,    "TRFPC", "TRFPC", writeRemovePlayerVotePayload,     readRemovePlayerVotePayload },
	{ NETCOMMANDTYPE_PLAYERRETURN,        "TRFPC", "TRFPC", writePlayerReturnPayload,         readPlayerReturnPayload },
};

const CommandLayout *layoutOf(Int type)
{
	for (const CommandLayout &layout : s_layouts) {
		if (layout.type == type) {
			return &layout;
		}
	}
	return nullptr;
}

Bool isAck(Int type)
{
	return type == NETCOMMANDTYPE_ACKBOTH || type == NETCOMMANDTYPE_ACKSTAGE1 || type == NETCOMMANDTYPE_ACKSTAGE2;
}

void ackFields(NetCommandMsg *cmd, UnsignedShort &commandID, UnsignedByte &originalPlayerID)
{
	switch (cmd->getNetCommandType()) {
		case NETCOMMANDTYPE_ACKBOTH:
			commandID = static_cast<NetAckBothCommandMsg *>(cmd)->getCommandID();
			originalPlayerID = static_cast<NetAckBothCommandMsg *>(cmd)->getOriginalPlayerID();
			break;
		case NETCOMMANDTYPE_ACKSTAGE1:
			commandID = static_cast<NetAckStage1CommandMsg *>(cmd)->getCommandID();
			originalPlayerID = static_cast<NetAckStage1CommandMsg *>(cmd)->getOriginalPlayerID();
			break;
		default:
			commandID = static_cast<NetAckStage2CommandMsg *>(cmd)->getCommandID();
			originalPlayerID = static_cast<NetAckStage2CommandMsg *>(cmd)->getOriginalPlayerID();
			break;
	}
}

template <typename AckMsg>
NetCommandMsg *initAck(AckMsg *msg, UnsignedShort commandID, UnsignedByte originalPlayerID)
{
	msg->setCommandID(commandID);
	msg->setOriginalPlayerID(originalPlayerID);
	return msg;
}

NetCommandMsg *newAck(Int type, UnsignedShort commandID, UnsignedByte originalPlayerID)
{
	switch (type) {
		case NETCOMMANDTYPE_ACKBOTH:   return initAck(newInstance(NetAckBothCommandMsg), commandID, originalPlayerID);
		case NETCOMMANDTYPE_ACKSTAGE1: return initAck(newInstance(NetAckStage1CommandMsg), commandID, originalPlayerID);
		default:                       return initAck(newInstance(NetAckStage2CommandMsg), commandID, originalPlayerID);
	}
}

// Writes a command outside a packet: all header fields, 'D', the data. Returns its size; with a
// null buffer only the size.
UnsignedInt writeFullCommand(NetCommandMsg *msg, UnsignedByte relay, UnsignedByte *buffer, std::size_t capacity)
{
	const CommandLayout *layout = layoutOf(msg->getNetCommandType());
	if (layout == nullptr) {
		DEBUG_CRASH(("Unknown NETCOMMANDTYPE %d", msg->getNetCommandType()));
		return 0;
	}
	PayloadWriter w(buffer, capacity);
	for (const char *field = layout->fullHeader; *field != 0; ++field) {
		w.put(static_cast<UnsignedByte>(*field));
		switch (*field) {
			case 'T': w.put(static_cast<UnsignedByte>(msg->getNetCommandType())); break;
			case 'F': w.put(msg->getExecutionFrame()); break;
			case 'R': w.put(relay); break;
			case 'P': w.put(static_cast<UnsignedByte>(msg->getPlayerID())); break;
			case 'C': w.put(msg->getID()); break;
		}
	}
	w.put(static_cast<UnsignedByte>('D'));
	layout->writePayload(w, msg);
	return w.length();
}

NetCommandMsg *readPayload(Int type, UnsignedByte *data, Int &i, UnsignedInt dataLength)
{
	const CommandLayout *layout = layoutOf(type);
	if (layout == nullptr) {
		return nullptr;
	}
	PacketReader r(data, i, dataLength);
	return layout->readPayload(r);
}

}	// namespace

// Checks against the received packet length, not the buffer size.
Bool NetPacket::PacketHasBytes(Int i, UnsignedInt need, UnsignedInt dataLength)
{
	if (i < 0 || static_cast<UnsignedInt>(i) > static_cast<UnsignedInt>(dataLength)) {
		return FALSE;
	}
	return need <= (static_cast<UnsignedInt>(dataLength) - static_cast<UnsignedInt>(i));
}

// Reads one command outside a packet (the reassembled data of a wrapper command). Fields that are
// missing keep their defaults; the data comes from the network and every field is checked.
NetCommandRef * NetPacket::ConstructNetCommandMsgFromRawData(UnsignedByte *data, UnsignedInt dataLength) {
	UnsignedByte commandType = NETCOMMANDTYPE_GAMECOMMAND;
	UnsignedShort commandID = 0;
	UnsignedInt frame = 0;
	UnsignedByte playerID = 0;
	UnsignedByte relay = 0;

	Int offset = 0;
	PacketReader r(data, offset, dataLength);
	while (offset < static_cast<Int>(dataLength)) {
		const UnsignedByte tag = data[offset++];
		Bool ok = TRUE;
		switch (tag) {
			case 'T': ok = r.get(commandType); break;
			case 'R': ok = r.get(relay); break;
			case 'P': ok = r.get(playerID); break;
			case 'C': ok = r.get(commandID); break;
			case 'F': ok = r.get(frame); break;
			case 'D': {
				NetCommandMsg *msg = readPayload(commandType, data, offset, dataLength);
				if (msg == nullptr) {
					DEBUG_LOG(("NetPacket::ConstructNetCommandMsgFromRawData - unknown command or damaged data (type %d), dropped\n", (int)commandType));
					return nullptr;
				}
				msg->setExecutionFrame(frame);
				msg->setID(commandID);
				msg->setPlayerID(playerID);
				msg->setNetCommandType(static_cast<NetCommandType>(commandType));
				NetCommandRef *ref = NEW_NETCOMMANDREF(msg);
				ref->setRelay(relay);
				msg->detach();
				return ref;
			}
			default:
				DEBUG_LOG(("NetPacket::ConstructNetCommandMsgFromRawData - unknown tag byte 0x%02X, dropped\n", (unsigned)tag));
				return nullptr;
		}
		if (!ok) {
			DEBUG_LOG(("NetPacket::ConstructNetCommandMsgFromRawData - data too short for '%c' field, dropped\n", tag));
			return nullptr;
		}
	}
	return nullptr;
}

// Splits a command too big for one packet into wrapper commands of one packet each.
NetPacketList NetPacket::ConstructBigCommandPacketList(NetCommandRef *ref) {
	NetCommandMsg *msg = ref->getCommand();

	// The receiver identifies the wrapped command by its command ID.
	if (!DoesCommandRequireACommandID(msg->getNetCommandType())) {
		DEBUG_CRASH(("Trying to wrap a command that doesn't have a unique command ID"));
		return NetPacketList();
	}

	const UnsignedInt bufferSize = writeFullCommand(msg, ref->getRelay(), nullptr, 0);
	auto bigPacketData = std::make_unique<UnsignedByte[]>(bufferSize);
	writeFullCommand(msg, ref->getRelay(), bigPacketData.get(), bufferSize);

	NetWrapperCommandMsg *wrapperMsg = newInstance(NetWrapperCommandMsg);
	// size of a wrapper command without its chunk data
	const UnsignedInt wrapperSize = writeFullCommand(wrapperMsg, 0, nullptr, 0);
	const UnsignedInt commandSizePerPacket = MAX_PACKET_SIZE - wrapperSize;

	UnsignedInt numChunks = bufferSize / commandSizePerPacket;
	if ((bufferSize % commandSizePerPacket) > 0) {
		++numChunks;
	}

	NetPacketList packetList;
	UnsignedInt offset = 0;
	for (UnsignedInt chunk = 0; chunk < numChunks; ++chunk) {
		const UnsignedInt chunkSize = std::min(commandSizePerPacket, bufferSize - offset);

		wrapperMsg->setID(GenerateNextCommandID());
		wrapperMsg->setPlayerID(msg->getPlayerID());
		wrapperMsg->setExecutionFrame(msg->getExecutionFrame());
		wrapperMsg->setChunkNumber(chunk);
		wrapperMsg->setNumChunks(numChunks);
		wrapperMsg->setDataOffset(offset);
		wrapperMsg->setData(bigPacketData.get() + offset, chunkSize);
		wrapperMsg->setTotalDataLength(bufferSize);
		wrapperMsg->setWrappedCommandID(msg->getID());
		offset += chunkSize;

		// The caller takes the wrapper commands out of these packets and sets their relay itself.
		NetCommandRef *wrapperRef = NEW_NETCOMMANDREF(wrapperMsg);
		wrapperRef->setRelay(0);

		NetPacket *packet = newInstance(NetPacket);
		if (packet->addCommand(wrapperRef) == FALSE) {
			DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("NetPacket::ConstructBigCommandPacketList - failed to add a wrapper command to the packet\n"));
		}
		packetList.push_back(packet);

		deleteInstanceOf(wrapperRef);
	}
	wrapperMsg->detach();

	return packetList;
}

/**
 * Constructor
 */
NetPacket::NetPacket() {
	init();
}

/**
 * Constructor given raw transport data.
 */
NetPacket::NetPacket(TransportMessage *msg) {
	init();
	// The transport accepts datagrams up to MAX_MESSAGE_LEN, but only MAX_PACKET_SIZE bytes are kept
	// here; a longer (or negative) length never comes from a valid sender and is read as empty.
	m_packetLen = msg->length;
	if (m_packetLen < 0 || m_packetLen > MAX_PACKET_SIZE) {
		DEBUG_LOG(("NetPacket - received packet length %d out of range, packet dropped\n", msg->length));
		m_packetLen = 0;
	}
	memcpy(m_packet, msg->data, MAX_PACKET_SIZE);
	m_numCommands = -1;
	m_addr = msg->addr;
	m_port = msg->port;
}

/**
 * Destructor
 */
NetPacket::~NetPacket() {
	if (m_lastCommand != nullptr) {
		deleteInstanceOf(m_lastCommand);
		m_lastCommand = nullptr;
	}
}

/**
 * Initialize all the member variable values.
 */
void NetPacket::init() {
	m_addr = 0;
	m_port = 0;
	m_numCommands = 0;
	m_packetLen = 0;
	m_packet[0] = 0;

	m_lastPlayerID = 0;
	m_lastFrame = 0;
	m_lastCommandID = 0;
	m_lastCommandType = 0;
	m_lastRelay = 0;

	m_lastCommand = nullptr;
}

void NetPacket::reset() {
	if (m_lastCommand != nullptr) {
		deleteInstanceOf(m_lastCommand);
		m_lastCommand = nullptr;
	}
	init();
}

/**
 * Set the address to which this packet is to be sent.
 */
void NetPacket::setAddress(Int addr, Int port) {
	m_addr = addr;
	m_port = static_cast<UnsignedShort>(port);
}

// Header of a command in this packet: the fields of the layout that differ from the previous
// command. Returns the size; with apply the fields are written and become the packet's state.
UnsignedInt NetPacket::writePackedHeader(NetCommandRef *ref, const char *fields, Bool apply) {
	NetCommandMsg *msg = ref->getCommand();
	UnsignedInt size = 0;
	Bool needCommandID = FALSE;
	for (const char *field = fields; *field != 0; ++field) {
		switch (*field) {
			case 'T':
				if (m_lastCommandType != msg->getNetCommandType()) {
					size += 2;
					if (apply) {
						m_lastCommandType = static_cast<UnsignedByte>(msg->getNetCommandType());
						put('T');
						put(m_lastCommandType);
					}
				}
				break;
			case 'F':
				if (m_lastFrame != msg->getExecutionFrame()) {
					size += 1 + static_cast<UnsignedInt>(sizeof(UnsignedInt));
					if (apply) {
						m_lastFrame = msg->getExecutionFrame();
						put('F');
						put(m_lastFrame);
					}
				}
				break;
			case 'R':
				if (m_lastRelay != ref->getRelay()) {
					size += 2;
					if (apply) {
						m_lastRelay = ref->getRelay();
						put('R');
						put(m_lastRelay);
					}
				}
				break;
			case 'P':
				if (m_lastPlayerID != msg->getPlayerID()) {
					size += 2;
					needCommandID = TRUE;
					if (apply) {
						m_lastPlayerID = static_cast<UnsignedByte>(msg->getPlayerID());
						put('P');
						put(m_lastPlayerID);
					}
				}
				break;
			case 'C':
				// the reader counts the command ID up by one per command that has one
				if (((m_lastCommandID + 1) != static_cast<UnsignedShort>(msg->getID())) || needCommandID) {
					size += 1 + static_cast<UnsignedInt>(sizeof(UnsignedShort));
					if (apply) {
						put('C');
						put(msg->getID());
					}
				}
				if (apply) {
					m_lastCommandID = msg->getID();
				}
				break;
		}
	}
	return size;
}

/**
 * Adds this command to the packet.  Returns false if there wasn't enough room
 * in the packet for this message, true otherwise.
 */
Bool NetPacket::addCommand(NetCommandRef *ref) {
	if (ref == nullptr) {
		return TRUE; // There was nothing to add, so it was successful.
	}
	NetCommandMsg *msg = ref->getCommand();
	const CommandLayout *layout = layoutOf(msg->getNetCommandType());
	if (layout == nullptr) {
		DEBUG_CRASH(("Unknown NETCOMMANDTYPE %d", msg->getNetCommandType()));
		return TRUE;
	}

	if (isRepeat(ref)) {
		if (m_packetLen >= MAX_PACKET_SIZE) {
			return FALSE;
		}
		put('Z');
		if (msg->getNetCommandType() == NETCOMMANDTYPE_FRAMEINFO) {
			// the repeated frame command advances the frame
			m_lastCommandID = msg->getID();
			++m_lastFrame;
		}
	} else {
		UnsignedByte payload[MAX_PACKET_SIZE];
		PayloadWriter w(payload, sizeof(payload));
		layout->writePayload(w, msg);
		const UnsignedInt size = writePackedHeader(ref, layout->packedHeader, FALSE) + 1 + w.length();
		if (size > static_cast<UnsignedInt>(MAX_PACKET_SIZE - m_packetLen)) {
			return FALSE;
		}
		writePackedHeader(ref, layout->packedHeader, TRUE);
		put('D');
		memcpy(m_packet + m_packetLen, payload, w.length());
		m_packetLen += w.length();
	}

	++m_numCommands;
	if (m_lastCommand != nullptr) {
		deleteInstanceOf(m_lastCommand);
	}
	m_lastCommand = NEW_NETCOMMANDREF(msg);
	m_lastCommand->setRelay(ref->getRelay());
	return TRUE;
}

// A frame command without commands for the frame after the previous one, or an ack for the
// command after the previously acked one, is sent as a single 'Z'.
Bool NetPacket::isRepeat(NetCommandRef *ref) {
	if (m_lastCommand == nullptr || ref->getRelay() != m_lastCommand->getRelay()) {
		return FALSE;
	}
	NetCommandMsg *msg = ref->getCommand();
	NetCommandMsg *last = m_lastCommand->getCommand();
	const NetCommandType type = msg->getNetCommandType();
	if (last->getNetCommandType() != type) {
		return FALSE;
	}
	if (type == NETCOMMANDTYPE_FRAMEINFO) {
		return static_cast<NetFrameCommandMsg *>(msg)->getCommandCount() == 0
			&& msg->getExecutionFrame() == last->getExecutionFrame() + 1
			&& msg->getID() == last->getID() + 1;
	}
	if (isAck(type)) {
		UnsignedShort commandID, lastCommandID;
		UnsignedByte originalPlayerID, lastOriginalPlayerID;
		ackFields(msg, commandID, originalPlayerID);
		ackFields(last, lastCommandID, lastOriginalPlayerID);
		return lastCommandID == commandID - 1 && lastOriginalPlayerID == originalPlayerID;
	}
	return FALSE;
}

/**
 * Returns the list of commands in this packet.
 */
NetCommandList * NetPacket::getCommandList() {
	NetCommandList *retval = newInstance(NetCommandList);
	retval->init();

	// Same defaults as the writer's state after init(); the first command ID is 1.
	UnsignedByte playerID = 0;
	UnsignedInt frame = 0;
	UnsignedShort commandID = 1;
	UnsignedByte commandType = 0;
	UnsignedByte relay = 0;
	NetCommandRef *lastCommand = nullptr;

	Int i = 0;
	PacketReader r(m_packet, i, static_cast<UnsignedInt>(m_packetLen));
	while (i < m_packetLen) {
		const UnsignedByte tag = m_packet[i++];
		Bool ok = TRUE;
		NetCommandMsg *msg = nullptr;
		switch (tag) {
			case 'T': ok = r.get(commandType); break;
			case 'F': ok = r.get(frame); break;
			case 'P': ok = r.get(playerID); break;
			case 'R': ok = r.get(relay); break;
			case 'C': ok = r.get(commandID); break;
			case 'D':
				msg = readPayload(commandType, m_packet, i, static_cast<UnsignedInt>(m_packetLen));
				if (msg == nullptr) {
					DEBUG_LOG(("NetPacket::getCommandList - unknown command or damaged packet (type %d), rest dropped\n", (int)commandType));
					ok = FALSE;
				}
				break;
			case 'Z':
				if (lastCommand == nullptr) {
					DEBUG_LOG(("NetPacket::getCommandList - 'Z' (repeat) without a previous command, rest dropped\n"));
					ok = FALSE;
				} else if (commandType == NETCOMMANDTYPE_FRAMEINFO) {
					NetFrameCommandMsg *frameMsg = newInstance(NetFrameCommandMsg);
					frameMsg->setCommandCount(0);
					++frame;
					msg = frameMsg;
				} else if (isAck(commandType) && lastCommand->getCommand()->getNetCommandType() == commandType) {
					// the ack after the previous one; the previous command must be the same ack type
					UnsignedShort lastAckedID;
					UnsignedByte originalPlayerID;
					ackFields(lastCommand->getCommand(), lastAckedID, originalPlayerID);
					msg = newAck(commandType, lastAckedID + 1, originalPlayerID);
				} else {
					DEBUG_LOG(("NetPacket::getCommandList - 'Z' for type %d after type %d, rest dropped\n", (int)commandType, (int)lastCommand->getCommand()->getNetCommandType()));
					ok = FALSE;
				}
				break;
			default:
				// No DEBUG_CRASH: an unknown byte is network data, not a programming error.
				DEBUG_LOG(("NetPacket::getCommandList - Unrecognized packet entry at index %d\n", i - 1));
				dumpPacketToLog();
				break;
		}
		if (!ok) {
			break;
		}
		if (msg == nullptr) {
			continue;
		}

		msg->setExecutionFrame(frame);
		msg->setPlayerID(playerID);
		msg->setNetCommandType(static_cast<NetCommandType>(commandType));
		msg->setID(commandID);
		if (DoesCommandRequireACommandID(static_cast<NetCommandType>(commandType))) {
			++commandID;
		}

		NetCommandRef *ref = retval->addMessage(msg);
		if (ref != nullptr) {
			ref->setRelay(relay);
		}

		if (lastCommand != nullptr) {
			deleteInstanceOf(lastCommand);
		}
		lastCommand = NEW_NETCOMMANDREF(msg);
		msg->detach();
	}

	if (lastCommand != nullptr) {
		deleteInstanceOf(lastCommand);
	}
	return retval;
}

/**
 * Returns the number of commands in this packet.  Only valid if the packet is locally constructed.
 */
Int NetPacket::getNumCommands() {
	return m_numCommands;
}

/**
 * Returns the address that this packet is to be sent to.  Only valid if the packet is locally constructed.
 */
UnsignedInt NetPacket::getAddr() {
	return m_addr;
}

/**
 * Returns the port that this packet is to be sent to.  Only valid if the packet is locally constructed.
 */
UnsignedShort NetPacket::getPort() {
	return m_port;
}

/**
 * Returns the data of this packet.
 */
UnsignedByte * NetPacket::getData() {
	return m_packet;
}

/**
 * Returns the length of the packet.
 */
Int NetPacket::getLength() {
	return m_packetLen;
}

/**
 * Dumps the packet to the debug log file
 */
void NetPacket::dumpPacketToLog() {
	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("NetPacket::dumpPacketToLog() - packet is %d bytes\n", m_packetLen));
	Int numLines = m_packetLen / 8;
	if ((m_packetLen % 8) != 0) {
		++numLines;
	}
	for (Int dumpindex = 0; dumpindex < numLines; ++dumpindex) {
		DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("\t%d\t", dumpindex*8));
		for (Int dumpindex2 = 0; (dumpindex2 < 8) && ((dumpindex*8 + dumpindex2) < m_packetLen); ++dumpindex2) {
			DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("%02x '%c' ", m_packet[dumpindex*8 + dumpindex2], m_packet[dumpindex*8 + dumpindex2]));
		}
		DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("\n"));
	}
	DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("End of packet dump\n"));
}
