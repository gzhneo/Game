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

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/ConnectionManager.h"

#include "GameLogic/GameLogic.h"
#include "GameNetwork/networkutil.h"

#include <cmath>

// ConnectionManager: run ahead and frame rate -- the packet router measures latencies and frame
// rates of all players and sets the run ahead for everyone.

// Seconds; far above any real latency, small enough for the run ahead computation.
static const Real MAX_REPORTED_LATENCY = 10.0f;

/**
 * A client has sent us their run ahead metrics, lets store them away for future calculations.
 */
void ConnectionManager::processRunAheadMetrics(NetRunAheadMetricsCommandMsg *msg) 
{
	UnsignedInt player = msg->getPlayerID();
	if ((player >= 0) && (player < MAX_SLOTS) && (isPlayerConnected(player))) {
		// The router turns these into the run ahead of everybody (updateRunAhead): a latency that is not
		// finite or out of range would overflow the conversion to frames. 0 means "not measured".
		Real latency = msg->getAverageLatency();
		if (!std::isfinite(latency) || latency < 0.0f)
			latency = 0.0f;
		if (latency > MAX_REPORTED_LATENCY)
			latency = MAX_REPORTED_LATENCY;
		m_latencyAverages[player] = latency;
		m_fpsAverages[player] = max(0, msg->getAverageFps());
		//DEBUG_LOG(("ConnectionManager::processRunAheadMetrics - player %d, fps = %d, latency = %f\n", player, msg->getAverageFps(), msg->getAverageLatency()));
		if (m_fpsAverages[player] > 100) {
			// limit the reported frame rate average to 100.  This is done because if a
			// user alt-tab's out of the game their frame rate climbs to in the neighborhood of
			// 300, that was deemed "ugly" by the powers that be.
			m_fpsAverages[player] = 100;
		}
	}
}

void ConnectionManager::updateRunAhead(Int oldRunAhead, Int frameRate, Bool didSelfSlug, Int nextExecutionFrame) {
	m_currentRunAhead = oldRunAhead;
	time_t curTime = timeGetTime();

	if ((m_lastRunAheadSent == 0) || ((curTime - m_lastRunAheadSent) > TheGlobalData->m_networkRunAheadMetricsTime)) {
		if (m_localSlot == m_packetRouterSlot) {
			// We are the packet router, time to compute a new run ahead for this game.
			m_latencyAverages[m_localSlot] = m_frameMetrics.getHighLatency();

			// since we are now using the display frame rate rather than the logic frame rate to get our average FPS,
			// it doesn't make sense to send the desired logic frame rate if we "slugged" ourself.
//			if (didSelfSlug) {
//				m_fpsAverages[m_localSlot] = frameRate;
//			} else {
				m_fpsAverages[m_localSlot] = m_frameMetrics.getAverageFPS();
//			}
			if (didSelfSlug) {
				//DEBUG_LOG(("ConnectionManager::updateRunAhead - local player run ahead metrics, fps = %d, actual fps = %d, latency = %f, didSelfSlug = true\n", m_fpsAverages[m_localSlot], m_frameMetrics.getAverageFPS(), m_latencyAverages[m_localSlot]));
			} else {
				//DEBUG_LOG(("ConnectionManager::updateRunAhead - local player run ahead metrics, fps = %d, latency = %f, didSelfSlug = false\n", m_fpsAverages[m_localSlot], m_latencyAverages[m_localSlot]));
			}
			Int minFps;
			Int minFpsPlayer;
			getMinimumFps(minFps, minFpsPlayer);
			DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::updateRunAhead - max latency = %f, min fps = %d, min fps player = %d old FPS = %d\n", getMaximumLatency(), minFps, minFpsPlayer, frameRate));
			if ((minFps >= ((frameRate * 9) / 10)) && (minFps < frameRate)) {
				// if the minimum fps is within 10% of the desired framerate, then keep the current minimum fps.
				minFps = frameRate;
			}
			if (minFps < 5) {
				minFps = 5; // Absolutely do not run below 5 fps.
			}
			// 0 means no limit; a frame rate of 0 would be refused by every receiver
			if (TheGlobalData->m_framesPerSecondLimit > 0 && minFps > TheGlobalData->m_framesPerSecondLimit) {
				minFps = TheGlobalData->m_framesPerSecondLimit; // Cap to 30 FPS.
			}
			DEBUG_LOG_LEVEL(DEBUG_LEVEL_NET, ("ConnectionManager::updateRunAhead - minFps after adjustment is %d\n", minFps));
			// Latencies are round trips including jitter (getHighLatency); up at once, down in damped steps.
			::Engine::RunAheadLimits limits;
			limits.minFrames = MIN_RUNAHEAD;
			limits.maxFrames = MAX_FRAMES_AHEAD / 2;
			limits.slackPercent = TheGlobalData->m_networkRunAheadSlack;
			const Int targetRunAhead = ::Engine::RunAheadController::target(getMaximumLatency(), minFps, limits);
			Int newRunAhead = m_runAheadController.next(oldRunAhead, targetRunAhead);
			if (newRunAhead != oldRunAhead) {
				DEBUG_LOG(("ConnectionManager::updateRunAhead - run ahead %d -> %d frames (target %d, round trip %f s, %d fps)\n",
					oldRunAhead, newRunAhead, targetRunAhead, getMaximumLatency(), minFps));
			}

			NetRunAheadCommandMsg *msg = newInstance(NetRunAheadCommandMsg);
			msg->setPlayerID(m_localSlot);
			if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
				msg->setID(GenerateNextCommandID());
			}

			// needs to be set to the greater of getExecutionFrame and TheGameLogic->getFrame() + oldRunAhead
			// This prevents the case of...
			// run ahead starts at 30
			// run ahead changes to 10 at frame 31 (the command was created on frame 1)
			// run ahead changes to 10 at frame 56 (the command was created on frame 46)
			// notice that 56 is within the previous run ahead of 30 which has triggered
			// the frame command count being set for frames 1 through 60 since run ahead
			// didn't change for the first time till frame 31.  This creates an extra command
			// for frame 56 that isn't accounted for in the frame command count that is sent
			// out in the NetFrameCommandMsg.  sheesh.
			if (static_cast<UnsignedInt>(nextExecutionFrame) > (TheGameLogic->getFrame() + oldRunAhead)) {
				msg->setExecutionFrame(nextExecutionFrame);
			} else {
				msg->setExecutionFrame(TheGameLogic->getFrame() + oldRunAhead);
			}

			msg->setRunAhead(static_cast<UnsignedShort>(newRunAhead));
			msg->setFrameRate(static_cast<UnsignedByte>(minFps));
			//DEBUG_LOG(("ConnectionManager::updateRunAhead - new run ahead = %d, new frame rate = %d, execution frame %d\n", newRunAhead, minFps, msg->getExecutionFrame()));
			sendLocalCommand(msg, static_cast<UnsignedByte>(0xff ^ (1 << minFpsPlayer))); // Send the packet to everyone but the lowest FPS player.

			NetRunAheadCommandMsg *msg2 = newInstance(NetRunAheadCommandMsg);
			msg2->setPlayerID(m_localSlot);
			if (DoesCommandRequireACommandID(msg2->getNetCommandType())) {
				/*
				 * Ok there needs to be a big friggin comment about this change...
				 * What happens is that the two run ahead commands get sent to different players
				 * using different command ID's.  So player 1 has the run ahead command as command x
				 * and player 2 has the command as command x+1.  This is all good except when it comes
				 * to players being disconnected.  With the new disconnect scheme player 1 could potentially
				 * send his run ahead command to player 2 (or the other way around) to let player 2 catch
				 * up to him.  So if player 2 has his run ahead command as x+1 and now he gets player 1's
				 * command list with the run ahead command listed as command x, he won't see them as being
				 * the same command and will now think he has two different run ahead commands for that frame
				 * and thus his command list will have an extra command and he will never be able to recover.
				 * So to fix this we have to use the same command ID for both run ahead commands.  That way
				 * when the commands are copied places for the disconnect screen they will be seen as the
				 * same command, and all will be good.
				 */
//				msg2->setID(GenerateNextCommandID());
				msg2->setID(msg->getID());
			}
			if (static_cast<UnsignedInt>(nextExecutionFrame) > (TheGameLogic->getFrame() + oldRunAhead)) {
				msg2->setExecutionFrame(nextExecutionFrame);
			} else {
				msg2->setExecutionFrame(TheGameLogic->getFrame() + oldRunAhead);
			}

			// Let the player with the slowest FPS run a little faster than the other computers...
			// just in case they are able to.  Then we might be able to run the game faster which would be good.
			Int newMinFps = (minFps * 11) / 10;
			if (newMinFps == minFps) {
				newMinFps = minFps + 1;
			}
			if (newMinFps > 30) {
				newMinFps = 30; // Cap FPS to 30.
			}
			msg2->setRunAhead(static_cast<UnsignedShort>(newRunAhead));
			msg2->setFrameRate(static_cast<UnsignedByte>(newMinFps));

			sendLocalCommand(msg2, static_cast<UnsignedByte>(1 << minFpsPlayer));

			msg->detach();
			msg2->detach();
		} else {
			// We are not the packet router, send our metrics info to the packet router.
			NetRunAheadMetricsCommandMsg *msg = newInstance(NetRunAheadMetricsCommandMsg);
			msg->setPlayerID(m_localSlot);
			if (DoesCommandRequireACommandID(msg->getNetCommandType())) {
				msg->setID(GenerateNextCommandID());
			}
			msg->setAverageLatency(m_frameMetrics.getHighLatency());

			// see above for explanation.
//			if (didSelfSlug) {
//				msg->setAverageFps(frameRate);
//			} else {
				msg->setAverageFps(m_frameMetrics.getAverageFPS());
//			}
			if (didSelfSlug) {
				//DEBUG_LOG(("ConnectionManager::updateRunAhead - average latency = %f, average fps = %d, actual fps = %d, didSelfSlug = true\n", m_frameMetrics.getAverageLatency(), m_frameMetrics.getAverageFPS(), m_frameMetrics.getAverageFPS()));
			} else {
				//DEBUG_LOG(("ConnectionManager::updateRunAhead - average latency = %f, average fps = %d, didSelfSlug = false\n", m_frameMetrics.getAverageLatency(), m_frameMetrics.getAverageFPS()));
			}
			m_connections[m_packetRouterSlot]->sendNetCommandMsg(msg, static_cast<UnsignedByte>(1 << m_packetRouterSlot));
			msg->detach();
		}
		m_lastRunAheadSent = curTime;
	}
}

Real ConnectionManager::getMaximumLatency() {
	// This works for 2 player games because the latency for the packet router is always 0.
	Real lat1 = 0.0;
	Real lat2 = 0.0;

	for (Int i = 0; i < MAX_SLOTS; ++i) {
		// nobody waits for observers, so their latency does not matter
		if (isPlayerConnected(i) && !isObserver(i)) {
			if (m_latencyAverages[i] != 0.0) {
				if (m_latencyAverages[i] > lat1) {
					lat2 = lat1;
					lat1 = m_latencyAverages[i];
				} else if (m_latencyAverages[i] > lat2) {
					lat2 = m_latencyAverages[i];
				}
			}
		}
	}

	return (lat1 + lat2);
}

void ConnectionManager::getMinimumFps(Int &minFps, Int &minFpsPlayer) {
	minFps = -1;
	minFpsPlayer = -1;
//	DEBUG_LOG(("ConnectionManager::getMinimumFps -"));
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		// an observer's frame rate does not slow the players (the packet router is a player if there is one)
		if (isObserver(i) && static_cast<UnsignedInt>(i) != m_localSlot) {
			continue;
		}
		if ((m_connections[i] != nullptr) || (static_cast<UnsignedInt>(i) == m_localSlot)) {
//			DEBUG_LOG((" %d: %d,", i, m_fpsAverages[i]));
			if (m_fpsAverages[i] != -1) {
				if ((minFps == -1) || (m_fpsAverages[i] < minFps)) {
					minFps = m_fpsAverages[i];
					minFpsPlayer = i;
				}
			}
		}
	}
//	DEBUG_LOG(("\n"));
}

UnsignedInt ConnectionManager::getMinimumCushion() {
	return m_frameMetrics.getMinimumCushion();
}

void ConnectionManager::setFrameGrouping(time_t frameGrouping) {
	// Since we are the packet router, we should send more packets per second since we
	// may become the latency bottleneck for sending packets from one player to the next.
	// This is probably ok since the packet router should have the fastest connection of all
	// the players in the game.
	if (m_localSlot == m_packetRouterSlot) {
		frameGrouping = frameGrouping / 2;
	}
	for (Int i = 0; i < MAX_SLOTS; ++i) {
		if (m_connections[i] != nullptr) {
			m_connections[i]->setFrameGrouping(frameGrouping);
		}
	}
}

/**
 * Return the smallest packet arrival cushion since this was last called.
 */
UnsignedInt ConnectionManager::getPacketArrivalCushion() {
	UnsignedInt retval = m_smallestPacketArrivalCushion;
	m_smallestPacketArrivalCushion = static_cast<UnsignedInt>(-1);
	return retval;
}

Int ConnectionManager::getAverageFPS( void )
{
	return m_frameMetrics.getAverageFPS();
}

Int ConnectionManager::getSlotAverageFPS(Int slot) {
	if ((slot < 0) || (slot >= MAX_SLOTS)) {
		return -1;
	}
	if ((m_packetRouterSlot != m_localSlot) && (static_cast<UnsignedInt>(slot) == m_localSlot)) {
		// our framerate data isn't valid for other players unless we are the
		// packet router, so don't fake someone out.
		return -1;
	}
	return m_fpsAverages[slot];
}
