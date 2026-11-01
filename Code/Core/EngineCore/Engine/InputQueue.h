/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// InputQueue.h -- rules for the input buffers between the window procedure and the next logic tick.
// Window messages are collected in every main loop pass (render-only frames too) and wait in the
// device buffers until the tick translates them.

#pragma once

#ifndef ENGINE_INPUTQUEUE_H
#define ENGINE_INPUTQUEUE_H

#include <cstdint>

namespace Engine
{

const unsigned int MOUSE_MOVE_MESSAGE = 0x0200;		// WM_MOUSEMOVE

/// May a new mouse message overwrite the previous one, still unread, in the buffer? Only a move after
/// a move with the same buttons and modifier keys (wParam): the tick only needs the latest position,
/// the deltas between the remaining events add up to the same distance, and a fast mouse does not fill
/// the buffer. A move after a button message stays, so presses keep their own position.
inline bool replacesPreviousMouseEvent(unsigned int previousMsg, std::uint64_t previousWParam,
	unsigned int msg, std::uint64_t wParam)
{
	return msg == MOUSE_MOVE_MESSAGE && previousMsg == MOUSE_MOVE_MESSAGE && previousWParam == wParam;
}

/// Milliseconds from the message time (GetMessageTime clock) to now (same clock); the clock wraps after
/// 49.7 days, the unsigned difference stays right across the wrap.
inline std::uint32_t inputAgeMs(std::uint32_t messageTime, std::uint32_t now)
{
	return now - messageTime;
}

/// Which real input a run takes. Image tests (-shotAfter, -shotEvery, -cameraPath) and test sequences
/// (-testablauf, which give their own mouse messages) ignore the real mouse: a cursor over the window would
/// change hover effects and the camera. -echteEingabeAus ignores mouse and keyboard.
struct RealInputFilter
{
	bool imageTest = false;
	bool testSequence = false;
	bool noRealInput = false;
};

inline bool acceptsRealMouse(const RealInputFilter &filter)
{
	return !filter.imageTest && !filter.testSequence && !filter.noRealInput;
}

inline bool acceptsRealKeyboard(const RealInputFilter &filter)
{
	return !filter.noRealInput;
}

}	// namespace Engine

#endif	// ENGINE_INPUTQUEUE_H
