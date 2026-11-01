/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowPrefabs.cpp -- prefabs: a selection saved under a name and placed again from the catalog
// ("Prefabs" under the map markers), on any map of the same game. A prefab is the selection in the
// clipboard format (map chunk data), one file per prefab in the editor's data folder, one folder per
// game since the object types differ.

#include "App/MainWindow.h"

#include "App/Language.h"
#include "Core/ObjectCommands.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QStandardPaths>

#include <span>

namespace MapEditorApp
{

namespace
{

constexpr const char *PREFAB_SUFFIX = ".prefab";

// Letters, digits, space, '-' and '_' stay; anything else would not do for a file name.
QString fileNameOf(const QString &name)
{
	QString out;
	for (const QChar c : name.trimmed())
		out += (c.isLetterOrNumber() || c == QLatin1Char(' ') || c == QLatin1Char('-') || c == QLatin1Char('_')) ? c : QLatin1Char('_');
	return out;
}

} // namespace

QString MainWindow::prefabFolder() const
{
	if (!m_prefabFolder.isEmpty())
		return m_prefabFolder;
	const QString base = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("Prefabs"));
	return QDir(base).filePath(m_state.game == Game::Generals ? QStringLiteral("Generals") : QStringLiteral("ZeroHour"));
}

QStringList MainWindow::prefabNames() const
{
	QStringList names;
	for (const QFileInfo &file : QDir(prefabFolder()).entryInfoList({QStringLiteral("*") + QLatin1String(PREFAB_SUFFIX)}, QDir::Files, QDir::Name))
		names << file.completeBaseName();
	return names;
}

bool MainWindow::savePrefab(const QString &name, QString *error)
{
	if (!m_state.map || m_state.map->selection.empty())
	{
		*error = T("Select the objects the prefab is to hold.");
		return false;
	}
	const QString file = fileNameOf(name);
	if (file.isEmpty())
	{
		*error = T("A prefab needs a name.");
		return false;
	}
	const MapEditorCore::Clipboard copies = MapEditorCore::copyObjects(m_state.map->map(), m_state.map->selection.objects);
	const std::vector<std::uint8_t> bytes = MapEditorCore::clipboardToBytes(copies);
	QDir().mkpath(prefabFolder());
	QFile out(QDir(prefabFolder()).filePath(file + QLatin1String(PREFAB_SUFFIX)));
	if (bytes.empty() || !out.open(QIODevice::WriteOnly)
		|| out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()))
	{
		*error = T("The prefab cannot be written: %1").arg(out.fileName());
		return false;
	}
	return true;
}

void MainWindow::saveSelectionAsPrefab()
{
	if (!m_state.map || m_state.map->selection.empty())
	{
		toast(T("Select the objects the prefab is to hold."));
		return;
	}
	bool ok = false;
	const QString name = QInputDialog::getText(this, T("Save as prefab"), T("Name:"), QLineEdit::Normal, QString(), &ok);
	if (!ok)
		return;
	QString error;
	if (!savePrefab(name, &error))
	{
		toast(error);
		return;
	}
	toast(T("Prefab %1 saved; it is in the catalog under Map markers \u203a Prefabs.").arg(fileNameOf(name)));
	if (m_workMode == WorkMode::Objects)
		rebuild();
}

bool MainWindow::placePrefab(const QString &name, float x, float y)
{
	if (!m_state.map)
		return false;
	QFile in(QDir(prefabFolder()).filePath(name + QLatin1String(PREFAB_SUFFIX)));
	if (!in.open(QIODevice::ReadOnly))
	{
		toast(T("The prefab cannot be read: %1").arg(in.fileName()));
		return false;
	}
	const QByteArray bytes = in.readAll();
	const std::optional<MapEditorCore::Clipboard> copies = MapEditorCore::clipboardFromBytes(
		std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(bytes.constData()), static_cast<std::size_t>(bytes.size())));
	if (!copies || copies->objects.empty())
	{
		toast(T("The prefab cannot be read: %1").arg(in.fileName()));
		return false;
	}
	// The middle of the prefab goes to the click.
	float cx = 0.0f;
	float cy = 0.0f;
	for (const MapModel::MapObject &o : copies->objects)
	{
		cx += o.x;
		cy += o.y;
	}
	cx /= static_cast<float>(copies->objects.size());
	cy /= static_cast<float>(copies->objects.size());
	auto command = MapEditorCore::pasteObjects(m_state.map->map(), *copies, snapCoord(x) - cx, snapCoord(y) - cy, "Place prefab");
	MapEditorCore::InsertObjects *inserted = command.get();
	m_state.map->history.execute(std::move(command), m_state.map->map());
	applyChanges(inserted->changes());
	return true;
}

void MainWindow::deletePrefabDialog()
{
	const QStringList names = prefabNames();
	if (names.isEmpty())
	{
		toast(T("There are no prefabs yet."));
		return;
	}
	bool ok = false;
	const QString name = QInputDialog::getItem(this, T("Delete prefab"), T("Prefab:"), names, 0, false, &ok);
	if (!ok)
		return;
	QFile::remove(QDir(prefabFolder()).filePath(name + QLatin1String(PREFAB_SUFFIX)));
	if (m_placeTemplate == QLatin1String(PREFAB_TOOL_PREFIX) + name)
		setPlacingTemplate(QString(), 0.0f);
	if (m_workMode == WorkMode::Objects)
		rebuild();
}

} // namespace MapEditorApp
