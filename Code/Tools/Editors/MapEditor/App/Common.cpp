/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "App/Common.h"

#include <QFile>

namespace MapEditorApp
{

std::string ascii(const QString &s)
{
	return s.toLatin1().toStdString();
}

QString latin(const std::string &s)
{
	return QString::fromLatin1(s.data(), static_cast<qsizetype>(s.size()));
}

std::filesystem::path toFsPath(const QString &path)
{
	return std::filesystem::path(path.toStdWString());
}

std::string readText(const QString &path)
{
	QFile file(path);
	return !path.isEmpty() && file.open(QIODevice::ReadOnly) ? file.readAll().toStdString() : std::string();
}

} // namespace MapEditorApp
