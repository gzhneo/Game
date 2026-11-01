/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Engine/PeerLink.h"

namespace Engine
{

PeerLink::PeerLink(std::uint32_t identity, std::uint32_t nonce, std::uint32_t nowMs, const PeerLinkTimings &timings)
	: m_timings(timings), m_identity(identity), m_nonce(nonce), m_since(nowMs), m_nextProbe(nowMs)
{
}

bool PeerLink::takeProbe(std::uint32_t nowMs)
{
	if (m_identity == 0 || !reached(nowMs, m_nextProbe))
		return false;
	const bool fast = !m_direct && !reached(nowMs, m_since + m_timings.fastPhase);
	m_nextProbe = nowMs + (fast ? m_timings.probeFast : m_timings.probeSlow);
	return true;
}

PeerLink::Change PeerLink::answered(std::uint32_t value, bool forceRelay)
{
	if (m_identity == 0 || value != m_nonce)
		return Change::None;
	m_direct = true;
	if (m_relayed && !forceRelay)
	{
		m_relayed = false;
		return Change::UseDirect;
	}
	return Change::None;
}

PeerLink::Change PeerLink::update(std::uint32_t nowMs, bool relayBound, bool forceRelay)
{
	if (m_identity == 0 || m_relayed || !relayBound)
		return Change::None;
	if (forceRelay || (!m_direct && reached(nowMs, m_since + m_timings.relayAfter)))
	{
		m_relayed = true;
		return Change::UseRelay;
	}
	return Change::None;
}

} // namespace Engine
