// Copyright (C) 2026 GZH Neo
// Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour (Electronic Arts).
// Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
//
// Differential test: the current NetPacket against the reference implementation (the EA
// serialization at the commit named in run.sh, compiled in as RefNetPacket). Random command
// sequences are written with both, packets and the results of addCommand must match byte for
// byte; big commands are split with both; packets (valid and mutated) are read with both and the
// commands must be equal.

#include "PreRTS.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/NetCommandMsg.h"
#include "GameNetwork/NetCommandList.h"
#include "GameNetwork/NetCommandRef.h"
#include "GameNetwork/networkutil.h"

#include "NetPacket_ref.h"	// reference implementation, renamed to RefNetPacket by run.sh

#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Rng
{
	UnsignedInt state;
	UnsignedInt next() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
	UnsignedInt below(UnsignedInt n) { return n ? next() % n : 0; }
	Bool chance(UnsignedInt percent) { return below(100) < percent; }
};

struct State
{
	UnsignedShort id = 1;
	UnsignedInt frame = 100;
	UnsignedInt player = 1;
};

template <typename T>
T *head(T *msg, NetCommandType type, Rng &rng, State &s)
{
	// mostly consecutive IDs and frames with occasional jumps, a few players: all header paths
	if (rng.chance(20)) s.player = rng.below(4);
	s.id = static_cast<UnsignedShort>(rng.chance(80) ? s.id + 1 : rng.next());
	if (rng.chance(40)) s.frame += rng.chance(80) ? 1 : rng.below(1000);
	msg->setNetCommandType(type);
	msg->setPlayerID(s.player);
	msg->setID(s.id);
	msg->setExecutionFrame(s.frame);
	return msg;
}

UnicodeString randomText(Rng &rng, UnsignedInt maxLen)
{
	std::wstring w(rng.below(maxLen + 1), L'a');
	for (wchar_t &c : w)
		c = static_cast<wchar_t>(rng.chance(90) ? L'a' + rng.below(26) : 0x100 + rng.below(0x2000));
	return UnicodeString(w.c_str());
}

NetCommandMsg *randomCommand(Rng &rng, State &s, Bool allowBig)
{
	static UnsignedByte data[5000];
	for (UnsignedByte &b : data)
		b = static_cast<UnsignedByte>(rng.next());
	switch (rng.below(27))
	{
		case 0: {
			NetGameCommandMsg *m = head(newInstance(NetGameCommandMsg), NETCOMMANDTYPE_GAMECOMMAND, rng, s);
			m->setGameMessageType(static_cast<GameMessage::Type>(rng.below(1500)));
			const UnsignedInt n = rng.below(allowBig ? 40 : 12);
			for (UnsignedInt k = 0; k < n; ++k)
			{
				GameMessageArgumentType a;
				memset(&a, 0, sizeof(a));
				const GameMessageArgumentDataType t = static_cast<GameMessageArgumentDataType>(rng.below(ARGUMENTDATATYPE_UNKNOWN));
				switch (t)
				{
					case ARGUMENTDATATYPE_LOCATION: a.location.x = static_cast<Real>(rng.below(5000)); a.location.y = 1.5f; a.location.z = -2.0f; break;
					case ARGUMENTDATATYPE_PIXEL: a.pixel.x = rng.below(2000); a.pixel.y = rng.below(2000); break;
					case ARGUMENTDATATYPE_PIXELREGION: a.pixelRegion.lo.x = rng.below(9); a.pixelRegion.hi.y = rng.below(900); break;
					case ARGUMENTDATATYPE_REAL: a.real = static_cast<Real>(rng.below(100000)) / 7.0f; break;
					case ARGUMENTDATATYPE_BOOLEAN: a.boolean = rng.chance(50); break;
					case ARGUMENTDATATYPE_WIDECHAR: a.wChar = static_cast<WideChar>(rng.below(0x3000)); break;
					default: a.integer = static_cast<Int>(rng.next()); break;
				}
				m->addArgument(t, a);
			}
			return m;
		}
		case 1: { NetAckBothCommandMsg *m = head(newInstance(NetAckBothCommandMsg), NETCOMMANDTYPE_ACKBOTH, rng, s);
			m->setCommandID(s.id); m->setOriginalPlayerID(static_cast<UnsignedByte>(rng.below(3))); return m; }
		case 2: { NetAckStage1CommandMsg *m = head(newInstance(NetAckStage1CommandMsg), NETCOMMANDTYPE_ACKSTAGE1, rng, s);
			m->setCommandID(s.id); m->setOriginalPlayerID(static_cast<UnsignedByte>(rng.below(3))); return m; }
		case 3: { NetAckStage2CommandMsg *m = head(newInstance(NetAckStage2CommandMsg), NETCOMMANDTYPE_ACKSTAGE2, rng, s);
			m->setCommandID(s.id); m->setOriginalPlayerID(static_cast<UnsignedByte>(rng.below(3))); return m; }
		case 4: { NetFrameCommandMsg *m = head(newInstance(NetFrameCommandMsg), NETCOMMANDTYPE_FRAMEINFO, rng, s);
			m->setCommandCount(static_cast<UnsignedShort>(rng.chance(70) ? 0 : rng.below(20))); return m; }
		case 5: { NetPlayerLeaveCommandMsg *m = head(newInstance(NetPlayerLeaveCommandMsg), NETCOMMANDTYPE_PLAYERLEAVE, rng, s);
			m->setLeavingPlayerID(static_cast<UnsignedByte>(rng.below(8))); return m; }
		case 6: { NetRunAheadMetricsCommandMsg *m = head(newInstance(NetRunAheadMetricsCommandMsg), NETCOMMANDTYPE_RUNAHEADMETRICS, rng, s);
			m->setAverageLatency(static_cast<Real>(rng.below(1000)) / 1000.0f); m->setAverageFps(static_cast<Int>(rng.below(60))); return m; }
		case 7: { NetRunAheadCommandMsg *m = head(newInstance(NetRunAheadCommandMsg), NETCOMMANDTYPE_RUNAHEAD, rng, s);
			m->setRunAhead(static_cast<UnsignedShort>(rng.below(100))); m->setFrameRate(static_cast<UnsignedByte>(rng.below(60))); return m; }
		case 8: { NetDestroyPlayerCommandMsg *m = head(newInstance(NetDestroyPlayerCommandMsg), NETCOMMANDTYPE_DESTROYPLAYER, rng, s);
			m->setPlayerIndex(rng.below(8)); return m; }
		case 9: return head(newInstance(NetKeepAliveCommandMsg), NETCOMMANDTYPE_KEEPALIVE, rng, s);
		case 10: return head(newInstance(NetDisconnectKeepAliveCommandMsg), NETCOMMANDTYPE_DISCONNECTKEEPALIVE, rng, s);
		case 11: { NetDisconnectPlayerCommandMsg *m = head(newInstance(NetDisconnectPlayerCommandMsg), NETCOMMANDTYPE_DISCONNECTPLAYER, rng, s);
			m->setDisconnectSlot(static_cast<UnsignedByte>(rng.below(8))); m->setDisconnectFrame(s.frame + rng.below(5)); return m; }
		case 12: return head(newInstance(NetPacketRouterQueryCommandMsg), NETCOMMANDTYPE_PACKETROUTERQUERY, rng, s);
		case 13: return head(newInstance(NetPacketRouterAckCommandMsg), NETCOMMANDTYPE_PACKETROUTERACK, rng, s);
		case 14: { NetDisconnectChatCommandMsg *m = head(newInstance(NetDisconnectChatCommandMsg), NETCOMMANDTYPE_DISCONNECTCHAT, rng, s);
			m->setText(randomText(rng, 80)); return m; }
		case 15: { NetChatCommandMsg *m = head(newInstance(NetChatCommandMsg), NETCOMMANDTYPE_CHAT, rng, s);
			m->setText(randomText(rng, allowBig ? 400 : 80)); m->setPlayerMask(static_cast<Int>(rng.below(256))); return m; }
		case 16: { NetDisconnectVoteCommandMsg *m = head(newInstance(NetDisconnectVoteCommandMsg), NETCOMMANDTYPE_DISCONNECTVOTE, rng, s);
			m->setSlot(static_cast<UnsignedByte>(rng.below(8))); m->setVoteFrame(s.frame); return m; }
		case 17: { NetProgressCommandMsg *m = head(newInstance(NetProgressCommandMsg), NETCOMMANDTYPE_PROGRESS, rng, s);
			m->setPercentage(static_cast<UnsignedByte>(rng.below(101))); return m; }
		case 18: return head(newInstance(NetCommandMsg), NETCOMMANDTYPE_LOADCOMPLETE, rng, s);
		case 19: return head(newInstance(NetCommandMsg), NETCOMMANDTYPE_TIMEOUTSTART, rng, s);
		case 20: { NetWrapperCommandMsg *m = head(newInstance(NetWrapperCommandMsg), NETCOMMANDTYPE_WRAPPER, rng, s);
			const UnsignedInt len = rng.below(allowBig ? 600 : 200);
			m->setData(data, len); m->setChunkNumber(rng.below(4)); m->setNumChunks(4); m->setTotalDataLength(len * 4);
			m->setDataOffset(rng.below(4) * len); m->setWrappedCommandID(static_cast<UnsignedShort>(rng.next())); return m; }
		case 21: { NetFileCommandMsg *m = head(newInstance(NetFileCommandMsg), NETCOMMANDTYPE_FILE, rng, s);
			m->setPortableFilename(AsciiString(rng.chance(50) ? "userdata\\maps\\a\\a.map" : "maps\\b\\map.ini"));
			m->setFileData(data, rng.below(allowBig ? 5000 : 300)); return m; }
		case 22: { NetFileAnnounceCommandMsg *m = head(newInstance(NetFileAnnounceCommandMsg), NETCOMMANDTYPE_FILEANNOUNCE, rng, s);
			m->setPortableFilename(AsciiString("userdata\\maps\\c\\c.tga")); m->setFileID(static_cast<UnsignedShort>(rng.below(9)));
			m->setPlayerMask(static_cast<UnsignedByte>(rng.below(256))); return m; }
		case 23: { NetFileProgressCommandMsg *m = head(newInstance(NetFileProgressCommandMsg), NETCOMMANDTYPE_FILEPROGRESS, rng, s);
			m->setFileID(static_cast<UnsignedShort>(rng.below(9))); m->setProgress(static_cast<Int>(rng.below(101))); return m; }
		case 24: { NetDisconnectFrameCommandMsg *m = head(newInstance(NetDisconnectFrameCommandMsg), NETCOMMANDTYPE_DISCONNECTFRAME, rng, s);
			m->setDisconnectFrame(s.frame); return m; }
		case 25: { NetDisconnectScreenOffCommandMsg *m = head(newInstance(NetDisconnectScreenOffCommandMsg), NETCOMMANDTYPE_DISCONNECTSCREENOFF, rng, s);
			m->setNewFrame(s.frame + 1); return m; }
		default: { NetFrameResendRequestCommandMsg *m = head(newInstance(NetFrameResendRequestCommandMsg), NETCOMMANDTYPE_FRAMERESENDREQUEST, rng, s);
			m->setFrameToResend(s.frame - rng.below(10)); return m; }
	}
}

template <typename P>
std::vector<UnsignedByte> bytesOf(P *p)
{
	return std::vector<UnsignedByte>(p->getData(), p->getData() + p->getLength());
}

std::string hex(const std::vector<UnsignedByte> &b)
{
	static const char digits[] = "0123456789abcdef";
	std::string s;
	for (UnsignedByte c : b) { s += digits[c >> 4]; s += digits[c & 15]; }
	return s;
}

// One command in canonical form (reference writer, fresh packet) plus its relay.
std::vector<UnsignedByte> canonical(NetCommandRef *r)
{
	RefNetPacket *p = newInstance(RefNetPacket);
	NetCommandRef *c = NEW_NETCOMMANDREF(r->getCommand());
	c->setRelay(r->getRelay());
	std::vector<UnsignedByte> out;
	if (p->addCommand(c))
		out = bytesOf(p);
	else
		out.push_back(0xEE);	// did not fit a single packet
	c->deleteInstance();
	p->deleteInstance();
	return out;
}

// Command types added after the reference (the vote to remove a player): a mutated packet may name
// one; the current code reads it, the reference stops there. Such reads are counted, not compared.
const Int FIRST_NEWER_TYPE = NETCOMMANDTYPE_REMOVEPLAYERVOTE;

Bool isNewerType(NetCommandRef *r)
{
	return r != nullptr && r->getCommand() != nullptr && r->getCommand()->getNetCommandType() >= FIRST_NEWER_TYPE;
}

Bool hasNewerType(NetCommandList *list)
{
	for (NetCommandRef *r = list ? list->getFirstMessage() : nullptr; r != nullptr; r = r->getNext())
		if (isNewerType(r))
			return TRUE;
	return FALSE;
}

std::vector<std::vector<UnsignedByte>> listForm(NetCommandList *list)
{
	std::vector<std::vector<UnsignedByte>> out;
	for (NetCommandRef *r = list ? list->getFirstMessage() : nullptr; r != nullptr; r = r->getNext())
		out.push_back(canonical(r));
	return out;
}

TransportMessage *transportOf(const std::vector<UnsignedByte> &bytes)
{
	static TransportMessage tm;
	memset(&tm, 0, sizeof(tm));
	memcpy(tm.data, bytes.data(), bytes.size());
	tm.length = static_cast<Int>(bytes.size());
	return &tm;
}

// newInstance pastes the class name into a token, so no template here
NetCommandList *readCurrent(const std::vector<UnsignedByte> &bytes)
{
	NetPacket *packet = newInstance(NetPacket)(transportOf(bytes));
	NetCommandList *list = packet->getCommandList();
	packet->deleteInstance();
	return list;
}

NetCommandList *readReference(const std::vector<UnsignedByte> &bytes)
{
	RefNetPacket *packet = newInstance(RefNetPacket)(transportOf(bytes));
	NetCommandList *list = packet->getCommandList();
	packet->deleteInstance();
	return list;
}

std::vector<UnsignedByte> mutate(Rng &rng, std::vector<UnsignedByte> d)
{
	const UnsignedInt steps = 1 + rng.below(4);
	for (UnsignedInt k = 0; k < steps && !d.empty(); ++k)
	{
		switch (rng.below(4))
		{
			case 0: d[rng.below(static_cast<UnsignedInt>(d.size()))] ^= static_cast<UnsignedByte>(1u << rng.below(8)); break;
			case 1: d[rng.below(static_cast<UnsignedInt>(d.size()))] = static_cast<UnsignedByte>(rng.next()); break;
			case 2: d.resize(rng.below(static_cast<UnsignedInt>(d.size()))); break;
			default: d.insert(d.begin() + rng.below(static_cast<UnsignedInt>(d.size()) + 1), static_cast<UnsignedByte>(rng.next())); break;
		}
	}
	if (d.size() > MAX_PACKET_SIZE)
		d.resize(MAX_PACKET_SIZE);
	return d;
}

}	// namespace

int runDiff(UnsignedInt sequences, UnsignedInt seed)
{
	Rng rng{seed ? seed : 1};
	Int failures = 0;
	UnsignedInt packets = 0, commands = 0, bigs = 0, reads = 0, newer = 0;
	auto fail = [&](const char *what, const std::string &a, const std::string &b)
	{
		if (failures < 20)
			printf("[FAIL] %s\n  current:   %s\n  reference: %s\n", what, a.c_str(), b.c_str());
		++failures;
	};

	for (UnsignedInt s = 0; s < sequences && failures < 20; ++s)
	{
		State st;
		st.frame = rng.below(100000);
		st.id = static_cast<UnsignedShort>(rng.next());
		NetPacket *cur = newInstance(NetPacket);
		RefNetPacket *ref = newInstance(RefNetPacket);
		std::vector<std::vector<UnsignedByte>> written;
		const UnsignedInt n = 1 + rng.below(40);
		UnsignedByte relay = static_cast<UnsignedByte>(rng.below(256));
		for (UnsignedInt k = 0; k < n; ++k)
		{
			NetCommandMsg *msg = randomCommand(rng, st, FALSE);
			if (rng.chance(10))
				relay = static_cast<UnsignedByte>(rng.below(256));
			NetCommandRef *r = NEW_NETCOMMANDREF(msg);
			r->setRelay(relay);
			msg->detach();
			const Bool a = cur->addCommand(r);
			const Bool b = ref->addCommand(r);
			++commands;
			if (a != b)
				fail("addCommand result differs", a ? "fits" : "full", b ? "fits" : "full");
			if (!a || !b)
			{
				if (bytesOf(cur) != bytesOf(ref))
					fail("full packet differs", hex(bytesOf(cur)), hex(bytesOf(ref)));
				written.push_back(bytesOf(ref));
				cur->deleteInstance(); ref->deleteInstance();
				cur = newInstance(NetPacket); ref = newInstance(RefNetPacket);
				const Bool a2 = cur->addCommand(r);
				const Bool b2 = ref->addCommand(r);
				if (a2 != b2)
					fail("addCommand into empty packet differs", a2 ? "fits" : "full", b2 ? "fits" : "full");
			}
			r->deleteInstance();
		}
		if (bytesOf(cur) != bytesOf(ref))
			fail("packet differs", hex(bytesOf(cur)), hex(bytesOf(ref)));
		if (cur->getNumCommands() != ref->getNumCommands())
			fail("command count differs", std::to_string(cur->getNumCommands()), std::to_string(ref->getNumCommands()));
		written.push_back(bytesOf(ref));
		cur->deleteInstance();
		ref->deleteInstance();
		packets += static_cast<UnsignedInt>(written.size());

		// read: the packets as written and mutated
		for (const std::vector<UnsignedByte> &w : written)
		{
			for (Int m = 0; m < 3; ++m)
			{
				const std::vector<UnsignedByte> in = m == 0 ? w : mutate(rng, w);
				NetCommandList *lc = readCurrent(in);
				NetCommandList *lr = readReference(in);
				++reads;
				std::vector<std::vector<UnsignedByte>> fc = listForm(lc), fr = listForm(lr);
				const Bool newerList = hasNewerType(lc);
				newer += newerList ? 1 : 0;
				if (!newerList && fc != fr)
				{
					std::string a = std::to_string(fc.size()) + " commands", b = std::to_string(fr.size()) + " commands, input " + hex(in);
					for (size_t k = 0; k < fc.size() && k < fr.size(); ++k)
						if (fc[k] != fr[k])
						{
							a += ", #" + std::to_string(k) + " " + hex(fc[k]);
							b += ", #" + std::to_string(k) + " " + hex(fr[k]);
							break;
						}
					fail("read differs", a, b);
				}
				if (lc) lc->deleteInstance();
				if (lr) lr->deleteInstance();

				NetCommandRef *rc = NetPacket::ConstructNetCommandMsgFromRawData(const_cast<UnsignedByte *>(in.data()), static_cast<UnsignedInt>(in.size()));
				NetCommandRef *rr = RefNetPacket::ConstructNetCommandMsgFromRawData(const_cast<UnsignedByte *>(in.data()), static_cast<UnsignedInt>(in.size()));
				const std::vector<UnsignedByte> cc = rc ? canonical(rc) : std::vector<UnsignedByte>();
				const std::vector<UnsignedByte> cr = rr ? canonical(rr) : std::vector<UnsignedByte>();
				if (cc != cr && !isNewerType(rc))
					fail("raw command differs", hex(cc), hex(cr));
				if (rc) rc->deleteInstance();
				if (rr) rr->deleteInstance();
			}
		}

		// big command split into wrapper packets
		if (rng.chance(10))
		{
			NetCommandMsg *msg = randomCommand(rng, st, TRUE);
			NetCommandRef *r = NEW_NETCOMMANDREF(msg);
			r->setRelay(relay);
			msg->detach();
			// The reference sized RunAheadMetrics, LoadComplete and TimeOutStart too small for their
			// data and wrote a wrapper without its data; none of them ever needs splitting.
			const NetCommandType type = msg->getNetCommandType();
			const Bool referenceSizeWrong = type == NETCOMMANDTYPE_RUNAHEADMETRICS || type == NETCOMMANDTYPE_LOADCOMPLETE
				|| type == NETCOMMANDTYPE_TIMEOUTSTART || type == NETCOMMANDTYPE_WRAPPER;
			if (DoesCommandRequireACommandID(type) && !referenceSizeWrong)
			{
				NetPacketList lc = NetPacket::ConstructBigCommandPacketList(r);
				// wrapper IDs come from a global 16-bit counter: wind it round so the reference
				// gets the same IDs
				for (size_t k = lc.size(); k < 65536; ++k)
					GenerateNextCommandID();
				RefNetPacketList lr = RefNetPacket::ConstructBigCommandPacketList(r);
				std::vector<std::vector<UnsignedByte>> bc, br;
				for (NetPacket *p : lc) { bc.push_back(bytesOf(p)); p->deleteInstance(); }
				for (RefNetPacket *p : lr) { br.push_back(bytesOf(p)); p->deleteInstance(); }
				if (bc != br)
					fail("big command split differs", std::to_string(bc.size()) + " packets", std::to_string(br.size()) + " packets");
				++bigs;
			}
			r->deleteInstance();
		}
	}
	printf("diff: %u sequences (seed %u), %u commands, %u packets, %u reads (%u with newer command types), %u big commands, %d differences\n",
		sequences, seed, commands, packets, reads, newer, bigs, failures);
	return failures ? 1 : 0;
}
