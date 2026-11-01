/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// RemovalVote.h -- the players of a network game vote to remove one of them.
//
// A vote is a command of the game's frames: every machine counts the same votes in the same frame
// and removes the player in that frame. A player is removed when every other player still in the
// game has voted against it within VOTE_FRAMES. All others rather than a majority, so that one
// side cannot remove its opponent. Observers do not vote (they can be removed); it takes at least
// two other voters, so in a game of two players there is no vote.
//
// No engine header: tested by EngineTests.

#pragma once

#include <array>
#include <cstdint>

namespace Engine
{

class RemovalVote
{
public:
	static constexpr int SLOTS = 8;
	static constexpr int MIN_VOTERS = 2;	///< voters besides the target
	static constexpr std::uint32_t VOTE_FRAMES = 30 * 120;	///< two minutes at 30 logic frames per second

	enum class Outcome
	{
		Refused,	///< not a valid vote: own slot, not in the game, an observer voting, too few voters
		Counted,
		Passed		///< every other player voted: remove the target now
	};

	struct Tally
	{
		int votes = 0;
		int needed = 0;
	};

	/// `voter` votes to remove `target` in `frame`. `inGame` has a bit for each player in the game,
	/// `voters` for each of them who votes (not the observers).
	Outcome vote(int voter, int target, std::uint32_t frame, std::uint8_t inGame, std::uint8_t voters)
	{
		voters &= inGame;
		if (!isSlot(voter) || !isSlot(target) || voter == target || !has(voters, voter) || !has(inGame, target))
			return Outcome::Refused;
		const Tally before = tally(target, frame, voters);
		if (before.needed < MIN_VOTERS)
			return Outcome::Refused;
		m_cast[target][voter] = frame + 1;
		const Tally t = tally(target, frame, voters);
		return t.votes >= t.needed ? Outcome::Passed : Outcome::Counted;
	}

	/// Valid votes against `target` in `frame`, and how many it takes.
	Tally tally(int target, std::uint32_t frame, std::uint8_t voters) const
	{
		Tally t;
		if (!isSlot(target))
			return t;
		for (int voter = 0; voter < SLOTS; ++voter)
		{
			if (voter == target || !has(voters, voter))
				continue;
			++t.needed;
			const std::uint32_t cast = m_cast[target][voter];
			if (cast != 0 && frame - (cast - 1) < VOTE_FRAMES)
				++t.votes;
		}
		return t;
	}

	/// `slot` is gone: its votes and the votes against it.
	void forget(int slot)
	{
		if (!isSlot(slot))
			return;
		for (int other = 0; other < SLOTS; ++other)
		{
			m_cast[slot][other] = 0;
			m_cast[other][slot] = 0;
		}
	}

	void clear() { m_cast = {}; }

private:
	static bool isSlot(int slot) { return slot >= 0 && slot < SLOTS; }
	static bool has(std::uint8_t mask, int slot) { return (mask >> slot) & 1; }

	std::array<std::array<std::uint32_t, SLOTS>, SLOTS> m_cast{};	///< [target][voter]: frame of the vote + 1, 0 for none
};

} // namespace Engine
