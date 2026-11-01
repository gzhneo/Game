/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// NetEmulator.h -- emulates a bad network link for received UDP packets.
//
// The transport hands every received packet to push() and takes the packets that are due with
// pop(). In between the emulator delays (latency plus jitter), drops, duplicates and reorders
// packets. All decisions come from a counter-based generator seeded by the caller, so a run with
// the same seed and the same packet/time sequence makes the same decisions.
//
// No engine header, no Windows: tested by EngineTests.

#pragma once

#ifndef ENGINE_NETEMULATOR_H
#define ENGINE_NETEMULATOR_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Engine
{

struct NetEmulatorSettings
{
	std::uint32_t latencyMs = 0;			///< fixed one-way delay
	std::uint32_t jitterMs = 0;				///< additional delay, uniform in [0, jitterMs]
	std::uint32_t lossPercent = 0;			///< packets dropped, 0..100
	std::uint32_t duplicatePercent = 0;		///< packets delivered twice, 0..100
	std::uint32_t reorderPercent = 0;		///< packets held back behind later ones, 0..100
	std::uint32_t seed = 0;

	/// True if any setting changes the packet stream.
	bool active() const
	{
		return latencyMs != 0 || jitterMs != 0 || lossPercent != 0 || duplicatePercent != 0 || reorderPercent != 0;
	}
};

struct NetEmulatorStats
{
	std::uint64_t received = 0;
	std::uint64_t dropped = 0;			///< by the loss setting
	std::uint64_t overflowed = 0;		///< queue full
	std::uint64_t duplicated = 0;
	std::uint64_t reordered = 0;
	std::uint64_t delivered = 0;
};

class NetEmulator
{
public:
	struct Packet
	{
		std::vector<std::uint8_t> data;
		std::uint32_t addr = 0;
		std::uint16_t port = 0;
		std::uint8_t tag = 0;	///< carried along unchanged (the transport marks authenticated packets)
	};

	/// Upper bound for queued packets; more are dropped and counted as overflow.
	static constexpr std::size_t MAX_QUEUED = 4096;
	/// Extra delay of a reordered packet on top of latency and jitter.
	static constexpr std::uint32_t MIN_REORDER_DELAY_MS = 20;

	/// Settings are clamped (percentages to 100, delays to 10 s). Clears the queue.
	void configure(const NetEmulatorSettings &settings);
	const NetEmulatorSettings &settings() const { return m_settings; }
	bool active() const { return m_settings.active(); }

	/// Offers a received packet at time nowMs. Returns the number of copies queued (0 = dropped).
	int push(const void *data, std::size_t length, std::uint32_t addr, std::uint16_t port, std::uint32_t nowMs, std::uint8_t tag = 0);

	/// Takes the next packet due at nowMs (earliest delivery time first, ties in arrival order).
	/// Returns false if none is due. Times may wrap around (timeGetTime).
	bool pop(std::uint32_t nowMs, Packet &out);

	std::size_t queued() const { return m_queue.size(); }
	const NetEmulatorStats &stats() const { return m_stats; }
	void clear();

private:
	struct Entry
	{
		std::uint32_t due;
		std::uint64_t order;
		Packet packet;
	};

	std::uint32_t nextRandom();
	bool chance(std::uint32_t percent);
	std::uint32_t delay();
	bool enqueue(const void *data, std::size_t length, std::uint32_t addr, std::uint16_t port, std::uint32_t due, std::uint8_t tag);

	NetEmulatorSettings m_settings;
	NetEmulatorStats m_stats;
	std::vector<Entry> m_queue;
	std::uint64_t m_counter = 0;
	std::uint64_t m_order = 0;
};

} // namespace Engine

#endif // ENGINE_NETEMULATOR_H
