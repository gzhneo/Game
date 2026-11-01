/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Network: a player dropped for not answering comes back (see ConnectionManagerReturn.cpp).
//
// Host: announcePlayerReturn sends a PLAYERRETURN for the execution frame E with the return frame
// R = E + run ahead + RETURN_LEAD_FRAMES. In frame E every machine takes the player in again; from R
// on they wait for its commands. At the start of R the host notes the network state for it.
//
// Returning player: from beginReturn on its frames come from the feed (the host's replay), without
// the frame rate while it is behind, and none of its own commands go out. Once the host names R it
// sends empty command counts for R and the `prefill` frames after it, so the others need not wait
// for it while it gets there, and its own commands go to later frames. Before R runs it takes over
// the host's state at R; from there it plays in the lockstep like everybody else.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/Network.h"

#include "GameLogic/GameLogic.h"
#include "GameNetwork/networkutil.h"

namespace
{
	/// Frames between the command and the return frame beyond the run ahead: time for the returning
	/// player to hear of it and for its first command counts to reach everybody.
	constexpr UnsignedInt RETURN_LEAD_FRAMES = 45;
	/// A return is not started this close to the end of the window.
	constexpr UnsignedInt RETURN_SPARE_FRAMES = 2 * LOGICFRAMES_PER_SECOND;
	/// The returning player runs without the frame rate while the feed is this far ahead of it.
	constexpr UnsignedInt CATCH_UP_FRAMES = 8;
	constexpr Int MAX_PREFILL = 150;
}

void Network::setReturnWindow(UnsignedInt seconds)
{
	if (m_conMgr != nullptr) {
		m_conMgr->setReturnWindow(seconds * LOGICFRAMES_PER_SECOND);
	}
}

Bool Network::isReturnable(Int slot)
{
	if (m_conMgr == nullptr || m_localStatus != NETLOCALSTATUS_INGAME || slot < 0 || slot >= MAX_SLOTS) {
		return FALSE;
	}
	// an observer's commands are executed nowhere, its announcement neither
	if (m_conMgr->isObserver(m_conMgr->getLocalPlayerID()) || !m_conMgr->isReturnable(slot) || m_conMgr->isPlayerConnected(slot)) {
		return FALSE;
	}
	const UnsignedInt window = m_conMgr->getReturnWindow();
	const UnsignedInt start = static_cast<UnsignedInt>(getExecutionFrame()) + m_runAhead + RETURN_LEAD_FRAMES;
	return window > 0 && start + RETURN_SPARE_FRAMES < m_conMgr->getDropFrame(slot) + window;
}

UnsignedInt Network::announcePlayerReturn(Int slot, const UnsignedByte *key, UnsignedShort port,
	const ::Engine::WireEndpoint &endpoint, UnsignedInt &prefill)
{
	prefill = 0;
	if (key == nullptr || !isReturnable(slot) || m_announcedReturn[slot] > TheGameLogic->getFrame()) {
		return 0;
	}
	const UnsignedInt executionFrame = static_cast<UnsignedInt>(getExecutionFrame());
	const UnsignedInt lead = m_runAhead + RETURN_LEAD_FRAMES;
	const UnsignedInt returnFrame = executionFrame + lead;
	// Its counts for frames past the return frame are kept in the frame ring of every machine, which
	// reaches FRAME_DATA_LENGTH - FRAMES_TO_KEEP frames ahead of the slowest, up to a run ahead behind us.
	Int ahead = FRAME_DATA_LENGTH - FRAMES_TO_KEEP - static_cast<Int>(lead) - 2 * m_runAhead - 16;
	if (ahead < 0) {
		ahead = 0;
	}
	if (ahead > MAX_PREFILL) {
		ahead = MAX_PREFILL;
	}

	NetPlayerReturnCommandMsg *msg = newInstance(NetPlayerReturnCommandMsg);
	msg->setSlot(static_cast<UnsignedByte>(slot));
	msg->setReturnFrame(returnFrame);
	msg->setPort(port);
	msg->setKey(key);
	msg->setEndpoint(endpoint.family, endpoint.address, endpoint.port);
	msg->setPlayerID(m_conMgr->getLocalPlayerID());
	msg->setExecutionFrame(executionFrame);
	if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
		msg->setID(GenerateNextCommandID());
	}
	m_conMgr->sendLocalCommand(msg);
	msg->detach();

	m_announcedReturn[slot] = returnFrame;
	m_returnStateReady &= static_cast<UnsignedByte>(~(1 << slot));
	prefill = static_cast<UnsignedInt>(ahead);
	DEBUG_LOG(("Network::announcePlayerReturn - slot %d in frame %d, its commands from frame %d on, %d frames counted ahead\n",
		slot, executionFrame, returnFrame, ahead));
	return returnFrame;
}

Bool Network::getReturnState(Int slot, ::Engine::SessionReturnState &out)
{
	if (slot < 0 || slot >= MAX_SLOTS || ((m_returnStateReady >> slot) & 1) == 0) {
		return FALSE;
	}
	out = m_returnStates[slot];
	return TRUE;
}

void Network::processPlayerReturn(NetPlayerReturnCommandMsg *msg)
{
	const Int slot = msg->getSlot();
	if (!m_conMgr->admitReturningPlayer(msg, static_cast<UnsignedInt>(m_lastFrameCompleted))) {
		return;
	}
	m_crcPending |= static_cast<UnsignedByte>(1 << slot);
	m_removalVote.forget(slot);
}

void Network::captureReturnStates(UnsignedInt frame)
{
	for (Int slot = 0; slot < MAX_SLOTS; ++slot) {
		if (m_announcedReturn[slot] == 0 || m_announcedReturn[slot] != frame + 1) {
			continue;
		}
		::Engine::SessionReturnState &state = m_returnStates[slot];
		m_conMgr->captureReturnState(state);
		state.frame = frame + 1;
		state.crcPending = static_cast<std::uint8_t>(m_crcPending & ~m_crcSkipped);
		state.runAhead = static_cast<std::uint16_t>(m_runAhead);
		state.frameRate = static_cast<std::uint16_t>(m_frameRate);
		m_returnStateReady |= static_cast<UnsignedByte>(1 << slot);
		DEBUG_LOG(("Network::captureReturnStates - state of frame %d for slot %d: players %X, router %d\n",
			state.frame, slot, state.connected, state.router));
	}
}

void Network::beginReturn(ReturnFeed *feed)
{
	if (m_conMgr == nullptr) {
		return;
	}
	m_returnFeed = feed;
	m_returnFrame = 0;
	m_returnStateApplied = FALSE;
	m_crcPending = 0;
	m_crcSkipped = 0;
	m_localStatus = NETLOCALSTATUS_RETURNING;
	m_conMgr->restartFrameData();
	DEBUG_LOG(("Network::beginReturn - back in the game as slot %d\n", m_conMgr->getLocalPlayerID()));
}

void Network::setReturnFrame(UnsignedInt frame, UnsignedInt prefill, UnsignedInt packetRouter)
{
	if (m_localStatus != NETLOCALSTATUS_RETURNING || m_returnFrame != 0 || frame == 0) {
		return;
	}
	m_returnFrame = frame;
	if (packetRouter < MAX_SLOTS) {
		m_conMgr->setPacketRouterSlot(packetRouter);
	}
	// nothing of ours in these frames: the others play on while we get there
	for (UnsignedInt f = frame; f <= frame + prefill; ++f) {
		m_conMgr->processFrameTick(f);
	}
	m_lastFrameCompleted = static_cast<Int>(frame + prefill);
	m_lastExecutionFrame = static_cast<Int>(frame + prefill + 1);
	DEBUG_LOG(("Network::setReturnFrame - commands from frame %d on, counted ahead to %d, packet router %d\n",
		frame, frame + prefill, packetRouter));
}

void Network::applyReturnState(const ::Engine::SessionReturnState &state)
{
	if (m_localStatus != NETLOCALSTATUS_RETURNING || m_returnFrame == 0 || state.frame != m_returnFrame || m_returnStateApplied) {
		return;
	}
	if (state.runAhead >= 1 && state.runAhead <= MAX_FRAMES_AHEAD) {
		m_runAhead = state.runAhead;
	}
	if (state.frameRate >= 1) {
		m_frameRate = state.frameRate;
	}
	setFrameGrouping();
	m_conMgr->applyReturnState(state);
	m_crcPending = static_cast<UnsignedByte>(state.crcPending | (1 << m_conMgr->getLocalPlayerID()));
	m_crcSkipped = 0;
	m_removalVote = ::Engine::RemovalVote();
	m_returnStateApplied = TRUE;
	DEBUG_LOG(("Network::applyReturnState - state of frame %d: players %X, router %d, run ahead %d\n",
		state.frame, state.connected, state.router, m_runAhead));
}

Bool Network::returningFrameReady(UnsignedInt frame)
{
	if (m_returnFrame == 0 || frame < m_returnFrame) {
		return m_returnFeed != nullptr && frame < m_returnFeed->completeFrames();
	}
	if (!m_returnStateApplied) {
		return FALSE;
	}
	// the return frame: from here on the lockstep
	DEBUG_LOG(("Network::returningFrameReady - in the game again on frame %d\n", frame));
	m_localStatus = NETLOCALSTATUS_INGAME;
	m_returnFeed = nullptr;
	return m_conMgr->allCommandsReady(frame);
}

Bool Network::isCatchingUp()
{
	return m_localStatus == NETLOCALSTATUS_RETURNING && m_returnFeed != nullptr
		&& TheGameLogic->getFrame() + CATCH_UP_FRAMES < m_returnFeed->completeFrames();
}

Bool Network::isCRCPending(Int slot)
{
	return slot >= 0 && slot < MAX_SLOTS && ((m_crcPending >> slot) & 1) != 0;
}

void Network::noteCRCSkipped(Int slot)
{
	if (slot >= 0 && slot < MAX_SLOTS) {
		m_crcSkipped |= static_cast<UnsignedByte>(1 << slot);
	}
}
