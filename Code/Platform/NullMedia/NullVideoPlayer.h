/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// NullVideoPlayer.h -- a video player that plays nothing, for builds without FFmpeg
// (GZH_NULL_MEDIA). The base class keeps the video list from Video.ini; its open() and load()
// return no stream, so the callers skip the movie.

#pragma once

#include "GameClient/VideoPlayer.h"

class NullVideoPlayer : public VideoPlayer
{
};
