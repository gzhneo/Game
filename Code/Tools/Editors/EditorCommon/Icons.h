/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Icons.h -- the editor's line icons: SVG on a 24 x 24 grid, stroke 1.75, coloured per theme.

#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace EditorCommon
{

// Names: select, height, texture, object, road, waypoint, area, water, light, script, layers,
// search, history, settings, file-new, folder, archive, play, check, close, chevron-down, sun, moon,
// grid, map, globe, info.
QIcon icon(const QString &name, const QColor &color);
QPixmap iconPixmap(const QString &name, const QColor &color, int size, qreal dpr = 1.0);

} // namespace EditorCommon
