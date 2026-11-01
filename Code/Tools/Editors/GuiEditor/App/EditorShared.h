/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EditorShared.h -- small helpers of the main window's source files.

#pragma once

#include "Model/WndLayout.h"

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <string>

namespace GuiEditorApp
{

// Layout files are CP1252; names and keys are ASCII.
inline QString latin1(const std::string &s)
{
	return QString::fromLatin1(s.data(), static_cast<qsizetype>(s.size()));
}

inline std::string toLatin1(const QString &s)
{
	return s.toLatin1().toStdString();
}

inline QString colorText(const WndModel::Color &c)
{
	return QStringLiteral("%1 %2 %3 %4").arg(c.r).arg(c.g).arg(c.b).arg(c.a);
}

inline QIcon swatch(const WndModel::Color &c)
{
	QPixmap pixmap(12, 12);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.fillRect(QRect(0, 0, 12, 12), QColor(128, 128, 128));
	painter.fillRect(QRect(1, 1, 10, 10), QColor(c.r, c.g, c.b, std::clamp(c.a, 0, 255)));
	return QIcon(pixmap);
}

// The text of a field on one line, whitespace collapsed.
inline QString oneLine(const std::string &value)
{
	return latin1(value).simplified();
}

inline std::uint32_t argbOf(const QColor &c, int alpha = 255)
{
	return (static_cast<std::uint32_t>(alpha) << 24) | (static_cast<std::uint32_t>(c.red()) << 16)
		| (static_cast<std::uint32_t>(c.green()) << 8) | static_cast<std::uint32_t>(c.blue());
}

} // namespace GuiEditorApp
