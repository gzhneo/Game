/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FILE: CameraPath.h /////////////////////////////////////////////////////////////////////////////
// Camera path for image comparisons.
//
// With -cameraPath <file> the client sets the camera to a pose from the file every tick, linearly
// interpolated between keys. The time base is the logic frame, so the same path gives the same
// images in a replay. The camera is not part of the replay CRC. Without the switch this module
// does nothing.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __CAMERAPATH_H_
#define __CAMERAPATH_H_

/// Set the camera path pose for the current logic frame (call before drawing).
void CameraPath_Update( void );

/// First and last key frame of the loaded path; FALSE while no path is loaded.
/// -shotEvery only captures within this range.
Bool CameraPath_GetFrameRange( UnsignedInt *first, UnsignedInt *last );

#endif // __CAMERAPATH_H_
