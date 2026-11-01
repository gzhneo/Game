// Copyright (C) 2026 GZH Neo
// Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour (Electronic Arts).
// Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
//
// Network packet tests (MinGW build, runs under Wine; tools/fuzz/netpacket/run.sh):
//   round trip  every command type is written with NetPacket::addCommand, read back with
//               getCommandList and written again; both byte strings must match, and they must match
//               the reference bytes in golden.txt (the wire format must not change by accident).
//   fuzz        the reference packets are mutated and fed to ConstructNetCommandMsgFromRawData (one
//               command, all read*Message functions) and to getCommandList (whole packet). Each input
//               ends right before an inaccessible page, so any read past the packet faults.
//
//   diff        random command sequences written and read by NetPacket and by the reference
//               implementation (RefNetPacket, NetPacketDiff.cpp) must agree byte for byte.
//   leaks       reading each reference packet and freeing its commands, and every fuzz input, must
//               return all allocations (global new and the pool stubs).
//
// Usage: NetPacketTests.exe <golden.txt> [--write-golden] [--fuzz <iterations>] [--diff <sequences>] [--seed <n>]

#include "PreRTS.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/NetCommandMsg.h"
#include "GameNetwork/NetCommandList.h"
#include "GameNetwork/NetCommandRef.h"
#include "GameNetwork/NetworkDefs.h"
#include "Common/NameKeyGenerator.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int runDiff(UnsignedInt sequences, UnsignedInt seed);	// NetPacketDiff.cpp

// Live allocations: global new here (declared in GameMemory.h without noexcept), pool blocks and dynamic memory in tools/linuxtests/poolstubs.cpp.
extern long g_poolStubLiveBlocks;
static long g_newLive = 0;

void *operator new(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (p == nullptr)
		throw std::bad_alloc();
	++g_newLive;
	return p;
}
void *operator new[](size_t n) { return operator new(n); }
void operator delete(void *p)
{
	if (p != nullptr)
	{
		--g_newLive;
		free(p);
	}
}
void operator delete[](void *p) { operator delete(p); }
void operator delete(void *p, size_t) noexcept { operator delete(p); }
void operator delete[](void *p, size_t) noexcept { operator delete(p); }

static long liveAllocations() { return g_newLive + g_poolStubLiveBlocks; }

namespace {

struct Sample
{
	const char *name;
	NetCommandMsg *msg;
};

template <typename T>
T *withType(T *msg, NetCommandType type)
{
	msg->setNetCommandType(type);
	msg->setPlayerID(2);
	msg->setID(0x1234);
	msg->setExecutionFrame(4321);
	return msg;
}

std::vector<Sample> makeSamples()
{
	std::vector<Sample> s;

	NetGameCommandMsg *game = withType(newInstance(NetGameCommandMsg), NETCOMMANDTYPE_GAMECOMMAND);
	game->setGameMessageType(static_cast<GameMessage::Type>(1001));
	GameMessageArgumentType arg;
	memset(&arg, 0, sizeof(arg));
	arg.integer = -17;							game->addArgument(ARGUMENTDATATYPE_INTEGER, arg);
	arg.real = 2.5f;							game->addArgument(ARGUMENTDATATYPE_REAL, arg);
	arg.boolean = TRUE;							game->addArgument(ARGUMENTDATATYPE_BOOLEAN, arg);
	arg.objectID = static_cast<ObjectID>(77);	game->addArgument(ARGUMENTDATATYPE_OBJECTID, arg);
	arg.drawableID = static_cast<DrawableID>(88);	game->addArgument(ARGUMENTDATATYPE_DRAWABLEID, arg);
	arg.teamID = 5;								game->addArgument(ARGUMENTDATATYPE_TEAMID, arg);
	arg.location.x = 1.0f; arg.location.y = 2.0f; arg.location.z = 3.0f;	game->addArgument(ARGUMENTDATATYPE_LOCATION, arg);
	memset(&arg, 0, sizeof(arg));
	arg.pixel.x = 640; arg.pixel.y = 480;		game->addArgument(ARGUMENTDATATYPE_PIXEL, arg);
	arg.pixelRegion.lo.x = 1; arg.pixelRegion.lo.y = 2; arg.pixelRegion.hi.x = 3; arg.pixelRegion.hi.y = 4;
	game->addArgument(ARGUMENTDATATYPE_PIXELREGION, arg);
	memset(&arg, 0, sizeof(arg));
	arg.timestamp = 123456;						game->addArgument(ARGUMENTDATATYPE_TIMESTAMP, arg);
	arg.wChar = L'Z';							game->addArgument(ARGUMENTDATATYPE_WIDECHAR, arg);
	s.push_back({"GAMECOMMAND", game});

	NetAckBothCommandMsg *ackBoth = withType(newInstance(NetAckBothCommandMsg), NETCOMMANDTYPE_ACKBOTH);
	ackBoth->setCommandID(0x4567); ackBoth->setOriginalPlayerID(3);
	s.push_back({"ACKBOTH", ackBoth});
	NetAckStage1CommandMsg *ack1 = withType(newInstance(NetAckStage1CommandMsg), NETCOMMANDTYPE_ACKSTAGE1);
	ack1->setCommandID(0x4568); ack1->setOriginalPlayerID(4);
	s.push_back({"ACKSTAGE1", ack1});
	NetAckStage2CommandMsg *ack2 = withType(newInstance(NetAckStage2CommandMsg), NETCOMMANDTYPE_ACKSTAGE2);
	ack2->setCommandID(0x4569); ack2->setOriginalPlayerID(5);
	s.push_back({"ACKSTAGE2", ack2});
	NetFrameCommandMsg *frame = withType(newInstance(NetFrameCommandMsg), NETCOMMANDTYPE_FRAMEINFO);
	frame->setCommandCount(9);
	s.push_back({"FRAMEINFO", frame});
	NetPlayerLeaveCommandMsg *leave = withType(newInstance(NetPlayerLeaveCommandMsg), NETCOMMANDTYPE_PLAYERLEAVE);
	leave->setLeavingPlayerID(6);
	s.push_back({"PLAYERLEAVE", leave});
	NetRunAheadMetricsCommandMsg *metrics = withType(newInstance(NetRunAheadMetricsCommandMsg), NETCOMMANDTYPE_RUNAHEADMETRICS);
	metrics->setAverageLatency(0.25f); metrics->setAverageFps(30);
	s.push_back({"RUNAHEADMETRICS", metrics});
	NetRunAheadCommandMsg *runAhead = withType(newInstance(NetRunAheadCommandMsg), NETCOMMANDTYPE_RUNAHEAD);
	runAhead->setRunAhead(12); runAhead->setFrameRate(30);
	s.push_back({"RUNAHEAD", runAhead});
	NetDestroyPlayerCommandMsg *destroy = withType(newInstance(NetDestroyPlayerCommandMsg), NETCOMMANDTYPE_DESTROYPLAYER);
	destroy->setPlayerIndex(7);
	s.push_back({"DESTROYPLAYER", destroy});
	s.push_back({"KEEPALIVE", withType(newInstance(NetKeepAliveCommandMsg), NETCOMMANDTYPE_KEEPALIVE)});
	s.push_back({"DISCONNECTKEEPALIVE", withType(newInstance(NetDisconnectKeepAliveCommandMsg), NETCOMMANDTYPE_DISCONNECTKEEPALIVE)});
	NetDisconnectPlayerCommandMsg *discPlayer = withType(newInstance(NetDisconnectPlayerCommandMsg), NETCOMMANDTYPE_DISCONNECTPLAYER);
	discPlayer->setDisconnectSlot(1); discPlayer->setDisconnectFrame(100);
	s.push_back({"DISCONNECTPLAYER", discPlayer});
	s.push_back({"PACKETROUTERQUERY", withType(newInstance(NetPacketRouterQueryCommandMsg), NETCOMMANDTYPE_PACKETROUTERQUERY)});
	s.push_back({"PACKETROUTERACK", withType(newInstance(NetPacketRouterAckCommandMsg), NETCOMMANDTYPE_PACKETROUTERACK)});
	NetDisconnectChatCommandMsg *discChat = withType(newInstance(NetDisconnectChatCommandMsg), NETCOMMANDTYPE_DISCONNECTCHAT);
	discChat->setText(UnicodeString(L"see you"));
	s.push_back({"DISCONNECTCHAT", discChat});
	NetChatCommandMsg *chat = withType(newInstance(NetChatCommandMsg), NETCOMMANDTYPE_CHAT);
	chat->setText(UnicodeString(L"hello all")); chat->setPlayerMask(0x0F);
	s.push_back({"CHAT", chat});
	NetDisconnectVoteCommandMsg *vote = withType(newInstance(NetDisconnectVoteCommandMsg), NETCOMMANDTYPE_DISCONNECTVOTE);
	vote->setSlot(2); vote->setVoteFrame(200);
	s.push_back({"DISCONNECTVOTE", vote});
	NetProgressCommandMsg *progress = withType(newInstance(NetProgressCommandMsg), NETCOMMANDTYPE_PROGRESS);
	progress->setPercentage(50);
	s.push_back({"PROGRESS", progress});
	s.push_back({"LOADCOMPLETE", withType(newInstance(NetCommandMsg), NETCOMMANDTYPE_LOADCOMPLETE)});
	s.push_back({"TIMEOUTSTART", withType(newInstance(NetCommandMsg), NETCOMMANDTYPE_TIMEOUTSTART)});
	static UnsignedByte payload[40];
	for (Int i = 0; i < 40; ++i)
		payload[i] = static_cast<UnsignedByte>(i * 7 + 1);
	NetWrapperCommandMsg *wrapper = withType(newInstance(NetWrapperCommandMsg), NETCOMMANDTYPE_WRAPPER);
	wrapper->setData(payload, sizeof(payload)); wrapper->setChunkNumber(0); wrapper->setNumChunks(1);
	wrapper->setTotalDataLength(sizeof(payload)); wrapper->setDataOffset(0); wrapper->setWrappedCommandID(0x2222);
	s.push_back({"WRAPPER", wrapper});
	NetFileCommandMsg *file = withType(newInstance(NetFileCommandMsg), NETCOMMANDTYPE_FILE);
	file->setPortableFilename(AsciiString("userdata\\maps\\test\\test.map")); file->setFileData(payload, sizeof(payload));
	s.push_back({"FILE", file});
	NetFileAnnounceCommandMsg *announce = withType(newInstance(NetFileAnnounceCommandMsg), NETCOMMANDTYPE_FILEANNOUNCE);
	announce->setPortableFilename(AsciiString("userdata\\maps\\test\\map.ini")); announce->setFileID(3); announce->setPlayerMask(0x06);
	s.push_back({"FILEANNOUNCE", announce});
	NetFileProgressCommandMsg *fileProgress = withType(newInstance(NetFileProgressCommandMsg), NETCOMMANDTYPE_FILEPROGRESS);
	fileProgress->setFileID(3); fileProgress->setProgress(40);
	s.push_back({"FILEPROGRESS", fileProgress});
	NetDisconnectFrameCommandMsg *discFrame = withType(newInstance(NetDisconnectFrameCommandMsg), NETCOMMANDTYPE_DISCONNECTFRAME);
	discFrame->setDisconnectFrame(300);
	s.push_back({"DISCONNECTFRAME", discFrame});
	NetDisconnectScreenOffCommandMsg *screenOff = withType(newInstance(NetDisconnectScreenOffCommandMsg), NETCOMMANDTYPE_DISCONNECTSCREENOFF);
	screenOff->setNewFrame(400);
	s.push_back({"DISCONNECTSCREENOFF", screenOff});
	NetFrameResendRequestCommandMsg *resend = withType(newInstance(NetFrameResendRequestCommandMsg), NETCOMMANDTYPE_FRAMERESENDREQUEST);
	resend->setFrameToResend(500);
	s.push_back({"FRAMERESENDREQUEST", resend});
	NetRemovePlayerVoteCommandMsg *removeVote = withType(newInstance(NetRemovePlayerVoteCommandMsg), NETCOMMANDTYPE_REMOVEPLAYERVOTE);
	removeVote->setSlot(3);
	s.push_back({"REMOVEPLAYERVOTE", removeVote});
	NetPlayerReturnCommandMsg *playerReturn = withType(newInstance(NetPlayerReturnCommandMsg), NETCOMMANDTYPE_PLAYERRETURN);
	UnsignedByte returnKey[NetPlayerReturnCommandMsg::KEY_SIZE];
	for (Int i = 0; i < NetPlayerReturnCommandMsg::KEY_SIZE; ++i)
		returnKey[i] = static_cast<UnsignedByte>(0xA0 + i);
	const UnsignedByte returnAddress[16] = { 192, 168, 7, 21 };
	playerReturn->setSlot(2); playerReturn->setReturnFrame(1234); playerReturn->setPort(8088);
	playerReturn->setKey(returnKey); playerReturn->setEndpoint(4, returnAddress, 8088);
	s.push_back({"PLAYERRETURN", playerReturn});
	return s;
}

std::vector<UnsignedByte> packetBytes(NetPacket *packet)
{
	return std::vector<UnsignedByte>(packet->getData(), packet->getData() + packet->getLength());
}

// Write one command into a fresh packet.
std::vector<UnsignedByte> writeOne(NetCommandMsg *msg)
{
	NetPacket *packet = newInstance(NetPacket);
	NetCommandRef *ref = NEW_NETCOMMANDREF(msg);
	std::vector<UnsignedByte> out;
	if (packet->addCommand(ref))
		out = packetBytes(packet);
	ref->deleteInstance();
	packet->deleteInstance();
	return out;
}

void freeList(NetCommandList *list)
{
	if (list)
		list->deleteInstance();
}

NetCommandList *readPacket(const std::vector<UnsignedByte> &bytes)
{
	static TransportMessage tm;
	memset(&tm, 0, sizeof(tm));
	memcpy(tm.data, bytes.data(), bytes.size());
	tm.length = static_cast<Int>(bytes.size());
	NetPacket *packet = newInstance(NetPacket)(&tm);
	NetCommandList *list = packet->getCommandList();
	packet->deleteInstance();
	return list;
}

// Several commands in one packet exercise the header compression (fields repeated only when they
// change, 'Z' for consecutive frames and acks).
struct Cmd
{
	NetCommandMsg *msg;
	UnsignedByte relay;
};

template <typename T>
T *cmd(T *msg, NetCommandType type, UnsignedInt player, UnsignedShort id, UnsignedInt frame)
{
	msg->setNetCommandType(type);
	msg->setPlayerID(player);
	msg->setID(id);
	msg->setExecutionFrame(frame);
	return msg;
}

NetFrameCommandMsg *frameCmd(UnsignedInt player, UnsignedShort id, UnsignedInt frame, UnsignedShort count)
{
	NetFrameCommandMsg *m = cmd(newInstance(NetFrameCommandMsg), NETCOMMANDTYPE_FRAMEINFO, player, id, frame);
	m->setCommandCount(count);
	return m;
}

NetAckBothCommandMsg *ackBothCmd(UnsignedInt player, UnsignedShort ackedID, UnsignedByte original)
{
	NetAckBothCommandMsg *m = cmd(newInstance(NetAckBothCommandMsg), NETCOMMANDTYPE_ACKBOTH, player, 0, 0);
	m->setCommandID(ackedID);
	m->setOriginalPlayerID(original);
	return m;
}

NetAckStage1CommandMsg *ack1Cmd(UnsignedInt player, UnsignedShort ackedID, UnsignedByte original)
{
	NetAckStage1CommandMsg *m = cmd(newInstance(NetAckStage1CommandMsg), NETCOMMANDTYPE_ACKSTAGE1, player, 0, 0);
	m->setCommandID(ackedID);
	m->setOriginalPlayerID(original);
	return m;
}

std::vector<std::pair<const char *, std::vector<Cmd>>> makeSequences()
{
	std::vector<std::pair<const char *, std::vector<Cmd>>> seqs;

	std::vector<Cmd> frames;
	frames.push_back({frameCmd(2, 10, 100, 3), 0x0E});
	for (UnsignedShort i = 1; i < 6; ++i)
		frames.push_back({frameCmd(2, static_cast<UnsignedShort>(10 + i), 100 + i, 0), 0x0E});
	frames.push_back({frameCmd(2, 20, 110, 0), 0x0E});		// gap: no repeat
	frames.push_back({frameCmd(2, 21, 111, 0), 0x0C});		// relay changes: no repeat
	seqs.push_back({"SEQ_FRAMES", frames});

	std::vector<Cmd> acks;
	for (UnsignedShort i = 0; i < 5; ++i)
		acks.push_back({ackBothCmd(1, static_cast<UnsignedShort>(50 + i), 3), 0x02});
	acks.push_back({ackBothCmd(1, 60, 3), 0x02});			// gap
	acks.push_back({ack1Cmd(1, 61, 3), 0x02});
	acks.push_back({ack1Cmd(1, 62, 3), 0x02});
	acks.push_back({ack1Cmd(4, 63, 3), 0x02});				// other player
	seqs.push_back({"SEQ_ACKS", acks});

	std::vector<Cmd> mixed;
	NetChatCommandMsg *chat = cmd(newInstance(NetChatCommandMsg), NETCOMMANDTYPE_CHAT, 3, 200, 300);
	chat->setText(UnicodeString(L"gg")); chat->setPlayerMask(0x0F);
	mixed.push_back({chat, 0xFF});
	mixed.push_back({cmd(newInstance(NetKeepAliveCommandMsg), NETCOMMANDTYPE_KEEPALIVE, 3, 0, 0), 0xFF});
	NetRunAheadCommandMsg *runAhead = cmd(newInstance(NetRunAheadCommandMsg), NETCOMMANDTYPE_RUNAHEAD, 2, 205, 301);
	runAhead->setRunAhead(15); runAhead->setFrameRate(30);
	mixed.push_back({runAhead, 0x07});
	NetPlayerLeaveCommandMsg *leave = cmd(newInstance(NetPlayerLeaveCommandMsg), NETCOMMANDTYPE_PLAYERLEAVE, 2, 206, 301);
	leave->setLeavingPlayerID(5);
	mixed.push_back({leave, 0x07});
	mixed.push_back({cmd(newInstance(NetDisconnectKeepAliveCommandMsg), NETCOMMANDTYPE_DISCONNECTKEEPALIVE, 4, 0, 0), 0x07});
	NetDisconnectVoteCommandMsg *vote = cmd(newInstance(NetDisconnectVoteCommandMsg), NETCOMMANDTYPE_DISCONNECTVOTE, 4, 207, 0);
	vote->setSlot(1); vote->setVoteFrame(299);
	mixed.push_back({vote, 0x07});
	NetProgressCommandMsg *progress = cmd(newInstance(NetProgressCommandMsg), NETCOMMANDTYPE_PROGRESS, 4, 0, 0);
	progress->setPercentage(75);
	mixed.push_back({progress, 0x07});
	seqs.push_back({"SEQ_MIXED", mixed});
	return seqs;
}

// The game takes outgoing commands from a NetCommandList, which keeps them sorted; do the same.
std::vector<Cmd> inListOrder(const std::vector<Cmd> &cmds)
{
	NetCommandList *list = newInstance(NetCommandList);
	list->init();
	for (const Cmd &c : cmds)
		list->addMessage(c.msg)->setRelay(c.relay);
	std::vector<Cmd> out;
	for (NetCommandRef *r = list->getFirstMessage(); r != nullptr; r = r->getNext())
		out.push_back({r->getCommand(), r->getRelay()});
	for (const Cmd &c : out)
		c.msg->attach();
	list->deleteInstance();
	for (const Cmd &c : out)
		c.msg->detach();
	return out;
}

// Write commands into packets; a new packet starts when one is full.
std::vector<std::vector<UnsignedByte>> writeSequence(const std::vector<Cmd> &cmds)
{
	std::vector<std::vector<UnsignedByte>> packets;
	NetPacket *packet = newInstance(NetPacket);
	for (const Cmd &c : cmds)
	{
		NetCommandRef *ref = NEW_NETCOMMANDREF(c.msg);
		ref->setRelay(c.relay);
		if (!packet->addCommand(ref))
		{
			packets.push_back(packetBytes(packet));
			packet->deleteInstance();
			packet = newInstance(NetPacket);
			packet->addCommand(ref);
		}
		ref->deleteInstance();
	}
	packets.push_back(packetBytes(packet));
	packet->deleteInstance();
	return packets;
}

// Read a packet and write its commands again, with the relay each one arrived with.
std::vector<UnsignedByte> rewritePacket(const std::vector<UnsignedByte> &bytes, Int *count)
{
	NetCommandList *list = readPacket(bytes);
	std::vector<Cmd> cmds;
	for (NetCommandRef *r = list ? list->getFirstMessage() : nullptr; r != nullptr; r = r->getNext())
		cmds.push_back({r->getCommand(), r->getRelay()});
	*count = static_cast<Int>(cmds.size());
	std::vector<std::vector<UnsignedByte>> out = writeSequence(cmds);
	freeList(list);
	return out.size() == 1 ? out[0] : std::vector<UnsignedByte>();
}

// A command too big for one packet is split into wrapper commands.
std::vector<std::vector<UnsignedByte>> splitBig(NetCommandMsg *msg)
{
	std::vector<std::vector<UnsignedByte>> out;
	NetCommandRef *ref = NEW_NETCOMMANDREF(msg);
	ref->setRelay(0x06);
	NetPacketList list = NetPacket::ConstructBigCommandPacketList(ref);
	for (NetPacket *p : list)
	{
		out.push_back(packetBytes(p));
		p->deleteInstance();
	}
	ref->deleteInstance();
	return out;
}

std::string hex(const std::vector<UnsignedByte> &b)
{
	static const char digits[] = "0123456789abcdef";
	std::string s;
	for (UnsignedByte c : b)
	{
		s += digits[c >> 4];
		s += digits[c & 15];
	}
	return s;
}

// Input placed so that its last byte is followed by a PAGE_NOACCESS page.
struct GuardedBuffer
{
	UnsignedByte *base = nullptr;
	SIZE_T pageSize = 0;

	GuardedBuffer()
	{
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		pageSize = si.dwPageSize;
		base = static_cast<UnsignedByte *>(VirtualAlloc(nullptr, 2 * pageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		DWORD old = 0;
		VirtualProtect(base + pageSize, pageSize, PAGE_NOACCESS, &old);
	}
	UnsignedByte *place(const std::vector<UnsignedByte> &data)
	{
		UnsignedByte *p = base + pageSize - data.size();
		if (!data.empty())
			memcpy(p, data.data(), data.size());
		return p;
	}
};

struct Rng
{
	UnsignedInt state;
	UnsignedInt next() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
	UnsignedInt below(UnsignedInt n) { return n ? next() % n : 0; }
};

std::vector<UnsignedByte> mutate(Rng &rng, const std::vector<std::vector<UnsignedByte>> &corpus)
{
	std::vector<UnsignedByte> d = corpus[rng.below(static_cast<UnsignedInt>(corpus.size()))];
	const UnsignedInt steps = 1 + rng.below(6);
	for (UnsignedInt s = 0; s < steps; ++s)
	{
		switch (rng.below(7))
		{
			case 0: if (!d.empty()) d[rng.below(static_cast<UnsignedInt>(d.size()))] ^= static_cast<UnsignedByte>(1u << rng.below(8)); break;
			case 1: if (!d.empty()) d[rng.below(static_cast<UnsignedInt>(d.size()))] = static_cast<UnsignedByte>(rng.next()); break;
			case 2: if (!d.empty()) d.resize(rng.below(static_cast<UnsignedInt>(d.size()))); break;
			case 3: d.insert(d.begin() + rng.below(static_cast<UnsignedInt>(d.size()) + 1), static_cast<UnsignedByte>(rng.next())); break;
			case 4:	// interesting values: lengths and counts at their limits
			{
				static const UnsignedByte values[] = { 0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF };
				if (!d.empty()) d[rng.below(static_cast<UnsignedInt>(d.size()))] = values[rng.below(6)];
				break;
			}
			case 5:	// splice with another corpus entry
			{
				const std::vector<UnsignedByte> &o = corpus[rng.below(static_cast<UnsignedInt>(corpus.size()))];
				const UnsignedInt cut = rng.below(static_cast<UnsignedInt>(d.size()) + 1);
				d.resize(cut);
				d.insert(d.end(), o.begin() + rng.below(static_cast<UnsignedInt>(o.size()) + 1), o.end());
				break;
			}
			default:	// type byte after a 'T' tag
				for (size_t i = 0; i + 1 < d.size(); ++i)
					if (d[i] == 'T') { d[i + 1] = static_cast<UnsignedByte>(rng.below(NETCOMMANDTYPE_MAX + 2)); break; }
				break;
		}
	}
	if (d.size() > MAX_PACKET_SIZE)
		d.resize(MAX_PACKET_SIZE);
	return d;
}

}	// namespace

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		printf("usage: NetPacketTests <golden.txt> [--write-golden] [--fuzz <iterations>] [--seed <n>]\n");
		return 2;
	}
	const char *goldenPath = argv[1];
	Bool writeGolden = FALSE;
	UnsignedInt iterations = 0, seed = 12345, diffSequences = 0;
	for (int i = 2; i < argc; ++i)
	{
		if (!strcmp(argv[i], "--write-golden")) writeGolden = TRUE;
		else if (!strcmp(argv[i], "--fuzz") && i + 1 < argc) iterations = static_cast<UnsignedInt>(strtoul(argv[++i], nullptr, 10));
		else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = static_cast<UnsignedInt>(strtoul(argv[++i], nullptr, 10));
		else if (!strcmp(argv[i], "--diff") && i + 1 < argc) diffSequences = static_cast<UnsignedInt>(strtoul(argv[++i], nullptr, 10));
	}

	// constructGameMessage turns player names into name keys
	::Engine::context().nameKeyGenerator = new NameKeyGenerator;
	TheNameKeyGenerator->init();

	// --- round trip
	int failures = 0;
	std::vector<Sample> samples = makeSamples();
	std::vector<std::vector<UnsignedByte>> corpus;
	std::string golden;
	for (const Sample &s : samples)
	{
		std::vector<UnsignedByte> first = writeOne(s.msg);
		if (first.empty())
		{
			printf("[FAIL] %s: addCommand failed\n", s.name);
			++failures;
			continue;
		}
		NetCommandList *list = readPacket(first);
		NetCommandRef *ref = list ? list->getFirstMessage() : nullptr;
		if (ref == nullptr)
		{
			printf("[FAIL] %s: read back no command\n", s.name);
			++failures;
			freeList(list);
			continue;
		}
		std::vector<UnsignedByte> second = writeOne(ref->getCommand());
		freeList(list);
		if (first != second)
		{
			printf("[FAIL] %s: written %s, rewritten %s\n", s.name, hex(first).c_str(), hex(second).c_str());
			++failures;
		}
		golden += std::string(s.name) + " " + hex(first) + "\n";
		corpus.push_back(first);
	}

	for (auto &seq : makeSequences())
	{
		std::vector<std::vector<UnsignedByte>> packets = writeSequence(inListOrder(seq.second));
		for (size_t k = 0; k < packets.size(); ++k)
		{
			Int count = 0;
			std::vector<UnsignedByte> again = rewritePacket(packets[k], &count);
			if (again != packets[k])
			{
				printf("[FAIL] %s packet %d: written %s, rewritten %s\n", seq.first, static_cast<int>(k),
					hex(packets[k]).c_str(), hex(again).c_str());
				++failures;
			}
			golden += std::string(seq.first) + "#" + std::to_string(k) + " " + hex(packets[k]) + "\n";
			corpus.push_back(packets[k]);
		}
		for (const Cmd &c : seq.second)
			c.msg->detach();
	}

	// big commands: chat with a long text and a file of 3000 bytes
	{
		NetChatCommandMsg *chat = cmd(newInstance(NetChatCommandMsg), NETCOMMANDTYPE_CHAT, 1, 400, 500);
		std::wstring text(300, L'x');
		for (size_t k = 0; k < text.size(); ++k)
			text[k] = static_cast<wchar_t>(L'a' + k % 26);
		chat->setText(UnicodeString(text.c_str()));
		chat->setPlayerMask(0x0F);
		NetFileCommandMsg *file = cmd(newInstance(NetFileCommandMsg), NETCOMMANDTYPE_FILE, 0, 401, 0);
		static UnsignedByte fileData[3000];
		for (Int k = 0; k < 3000; ++k)
			fileData[k] = static_cast<UnsignedByte>(k * 13 + 5);
		file->setPortableFilename(AsciiString("userdata\\maps\\big\\big.map"));
		file->setFileData(fileData, sizeof(fileData));
		const std::pair<const char *, NetCommandMsg *> bigs[] = { {"BIG_CHAT", chat}, {"BIG_FILE", file} };
		for (const auto &b : bigs)
		{
			std::vector<std::vector<UnsignedByte>> packets = splitBig(b.second);
			if (packets.empty() || (b.second == file && packets.size() < 2))
			{
				printf("[FAIL] %s: unexpected number of wrapper packets: %d\n", b.first, static_cast<int>(packets.size()));
				++failures;
			}
			for (size_t k = 0; k < packets.size(); ++k)
			{
				golden += std::string(b.first) + "#" + std::to_string(k) + " " + hex(packets[k]) + "\n";
				corpus.push_back(packets[k]);
			}
			b.second->detach();
		}
	}

	if (writeGolden)
	{
		FILE *f = fopen(goldenPath, "wb");
		if (!f) { printf("cannot write %s\n", goldenPath); return 2; }
		fputs(golden.c_str(), f);
		fclose(f);
		printf("golden written: %d command types\n", static_cast<int>(samples.size()));
	}
	else
	{
		std::string expected;
		FILE *f = fopen(goldenPath, "rb");
		if (!f) { printf("[FAIL] golden file %s missing\n", goldenPath); return 1; }
		char buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
			expected.append(buf, n);
		fclose(f);
		expected.erase(std::remove(expected.begin(), expected.end(), '\r'), expected.end());
		if (expected != golden)
		{
			printf("[FAIL] wire format differs from %s; actual:\n%s", goldenPath, golden.c_str());
			++failures;
		}
	}
	// A datagram longer than a packet (the transport accepts up to MAX_MESSAGE_LEN) must not be read
	// past m_packet; the bounds trap catches an index beyond it.
	{
		static TransportMessage tm;
		memset(&tm, 'X', sizeof(tm));
		tm.length = MAX_MESSAGE_LEN - static_cast<Int>(sizeof(TransportMessageHeader));
		NetPacket *packet = newInstance(NetPacket)(&tm);
		NetCommandList *list = packet->getCommandList();
		if (list->getFirstMessage() != nullptr)
		{
			printf("[FAIL] oversized datagram: commands read\n");
			++failures;
		}
		freeList(list);
		packet->deleteInstance();
	}
	printf("round trip: %d command types, %d failures\n", static_cast<int>(samples.size()), failures);

	// --- leaks: reading a packet and freeing its commands returns every allocation
	{
		int leaking = 0;
		for (size_t k = 0; k < corpus.size(); ++k)
		{
			const long before = liveAllocations();
			freeList(readPacket(corpus[k]));
			const long left = liveAllocations() - before;
			if (left != 0)
			{
				printf("[FAIL] leak: packet %d keeps %ld allocations after reading (%s)\n", static_cast<int>(k), left,
					hex(std::vector<UnsignedByte>(corpus[k].begin(), corpus[k].begin() + std::min<size_t>(corpus[k].size(), 16))).c_str());
				++leaking;
			}
		}
		printf("leaks: %d packets read, %d leaking\n", static_cast<int>(corpus.size()), leaking);
		failures += leaking;
	}

	// --- fuzz
	if (iterations)
	{
		GuardedBuffer guard;
		Rng rng{seed ? seed : 1};
		UnsignedInt accepted = 0, leakingInputs = 0;
		for (UnsignedInt it = 0; it < iterations; ++it)
		{
			std::vector<UnsignedByte> d = mutate(rng, corpus);
			const long before = liveAllocations();
			UnsignedByte *p = guard.place(d);
			NetCommandRef *ref = NetPacket::ConstructNetCommandMsgFromRawData(p, static_cast<UnsignedInt>(d.size()));
			if (ref)
			{
				++accepted;
				ref->deleteInstance();
			}
			freeList(readPacket(d));
			if (liveAllocations() != before && leakingInputs++ < 5)
				printf("[FAIL] leak: fuzz input %u keeps %ld allocations\n", it, liveAllocations() - before);
		}
		printf("fuzz: %u inputs (seed %u), %u parsed as a command, no fault\n", iterations, seed, accepted);
		printf("fuzz leaks: %u inputs leaking\n", leakingInputs);
		if (leakingInputs)
			++failures;
	}

	if (diffSequences && runDiff(diffSequences, seed))
		++failures;

	for (const Sample &s : samples)
		s.msg->detach();
	return failures ? 1 : 0;
}
