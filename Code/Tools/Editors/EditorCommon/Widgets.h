/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Widgets.h -- small building blocks with the object names the style sheet knows (Theme.cpp).

#pragma once

#include <QLabel>
#include <QString>
#include <QWidget>

#include <functional>
#include <string>
#include <vector>

class QDateTime;
class QPushButton;

namespace EditorCommon
{

QLabel *label(const QString &text, const char *objectName = nullptr, QWidget *parent = nullptr);
QWidget *hLine(QWidget *parent = nullptr);

// Row of mutually exclusive buttons in a rounded frame; onPick gets the index.
QWidget *segmented(const std::vector<QString> &items, int current, const std::function<void(int)> &onPick,
	QWidget *parent = nullptr, const std::vector<bool> &enabled = {});

// "3 hours ago" and the like.
QString relativeTime(const QDateTime &when);

// Text of the core and the map model (UTF-8) for the interface.
[[nodiscard]] inline QString utf8(const std::string &s)
{
	return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

// A child widget of the window by its object name, as the type asked for; null if missing or of
// another type.
template <typename T>
[[nodiscard]] T *child(const QWidget *window, const char *name)
{
	return window ? dynamic_cast<T *>(window->findChild<QWidget *>(QString::fromLatin1(name))) : nullptr;
}

} // namespace EditorCommon
