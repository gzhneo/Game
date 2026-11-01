/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowTools.cpp -- camera bookmarks, exporting the map as text (the old "Dump to file") and as
// a picture (in place of printing), and the about dialog.

#include "App/MainWindow.h"
#include "App/Widgets.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapImage.h"
#include "App/MapOverview.h"
#include "App/MiniMap.h"
#include "App/Overlay3D.h"
#include "App/Viewport3D.h"
#include "Core/CliffTexture.h"
#include "Model/MapDump.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>

#include <cmath>

namespace MapEditorApp
{

void MainWindow::centerOn(float x, float y)
{
	if (m_overview)
		m_overview->centerOn(x, y);
	if (m_viewport && m_viewport->hasDevice())
	{
		EngineClient &engine = EngineClient::instance();
		MeCamera camera = engine.camera();
		camera.targetX = x;
		camera.targetY = y;
		engine.setCamera(camera);
		m_viewport->update();
	}
	refreshCameraWindow();
}

void MainWindow::addBookmark()
{
	if (!m_state.map || !m_viewport || !m_viewport->hasDevice())
		return;
	std::vector<CameraBookmark> list = m_state.bookmarks(m_state.map->path);
	bool ok = false;
	const QString name = QInputDialog::getText(this, T("Camera bookmark"), T("Name:"), QLineEdit::Normal,
		T("View %1").arg(list.size() + 1), &ok).trimmed();
	if (!ok || name.isEmpty())
		return;
	const MeCamera c = EngineClient::instance().camera();
	list.push_back({name, c.targetX, c.targetY, c.targetZ, c.yaw, c.pitch, c.distance});
	m_state.setBookmarks(m_state.map->path, list);
	refreshCameraWindow();
}

void MainWindow::goToBookmark(int index)
{
	if (!m_state.map)
		return;
	const std::vector<CameraBookmark> list = m_state.bookmarks(m_state.map->path);
	if (index < 0 || index >= static_cast<int>(list.size()))
		return;
	const CameraBookmark &b = list[static_cast<std::size_t>(index)];
	if (m_overview)
		m_overview->centerOn(b.x, b.y);
	if (m_viewport && m_viewport->hasDevice())
	{
		EngineClient::instance().setCamera(MeCamera{b.x, b.y, b.z, b.yaw, b.pitch, b.distance});
		m_viewport->update();
	}
	refreshCameraWindow();
}

bool MainWindow::exportMapText(const QString &path, QString *error)
{
	if (!m_state.map)
		return false;
	const std::string text = MapModel::dumpMap(m_state.map->map(), MapModel::DumpDetail::Full);
	QFile out(path);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Text)
		|| out.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size()))
	{
		*error = T("The file cannot be written: %1").arg(path);
		return false;
	}
	return true;
}

bool MainWindow::exportMapPicture(const QString &path, int pixelsPerCell, QString *error)
{
	if (!m_state.map || !m_state.map->map().heightMap)
		return false;
	MapImageOptions options;
	options.objects = true;
	const QImage cells = renderMapImage(m_state.map->map(), options);
	if (cells.isNull())
	{
		*error = T("The map has no height map.");
		return false;
	}
	// Each cell a square of pixels, sharp as in the 2D overview.
	const QImage picture = cells.scaled(cells.width() * pixelsPerCell, cells.height() * pixelsPerCell, Qt::IgnoreAspectRatio, Qt::FastTransformation);
	if (!picture.save(path))
	{
		*error = T("The file cannot be written: %1").arg(path);
		return false;
	}
	return true;
}

void MainWindow::showMapIni()
{
	if (!m_state.map || m_state.map->path.isEmpty())
	{
		toast(T("Save the map first: its INI files lie beside it."));
		return;
	}
	const QDir folder = QFileInfo(m_state.map->path).dir();
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("MapIniWindow"));
	dialog.setWindowTitle(T("Map INI files"));
	dialog.resize(760, 560);
	auto *col = new QVBoxLayout(&dialog);
	auto *hint = new QLabel(T("The game reads these files when it loads the map: map.ini always, solo.ini in single player. They change INI values for this map only, for example the water (WaterSet, WaterTransparency) or weapons and objects of a mission. An empty file is removed."), &dialog);
	hint->setObjectName(QStringLiteral("Hint"));
	hint->setWordWrap(true);
	col->addWidget(hint);
	auto *tabs = new QTabWidget(&dialog);
	const QString names[] = {QStringLiteral("map.ini"), QStringLiteral("solo.ini")};
	QPlainTextEdit *edits[2] = {nullptr, nullptr};
	QFont mono(QStringLiteral("Consolas"));
	mono.setStyleHint(QFont::Monospace);
	for (int i = 0; i < 2; ++i)
	{
		auto *edit = new QPlainTextEdit(tabs);
		edit->setObjectName(names[i]);
		edit->setFont(mono);
		edit->setLineWrapMode(QPlainTextEdit::NoWrap);
		QFile file(folder.filePath(names[i]));
		if (file.open(QIODevice::ReadOnly))
			edit->setPlainText(QString::fromLatin1(file.readAll()));
		tabs->addTab(edit, names[i]);
		edits[i] = edit;
	}
	col->addWidget(tabs, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	col->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;
	for (int i = 0; i < 2; ++i)
	{
		const QString path = folder.filePath(names[i]);
		// INI files of the game are single-byte text.
		const QByteArray text = edits[i]->toPlainText().toLatin1();
		if (text.trimmed().isEmpty())
		{
			QFile::remove(path);
			continue;
		}
		QFile out(path);
		if (!out.open(QIODevice::WriteOnly) || out.write(text) != text.size())
			toast(T("The file cannot be written: %1").arg(path));
	}
	toast(T("Map INI files saved"));
}

void MainWindow::showAbout()
{
	QMessageBox::about(this, T("About the MapEditor"),
		T("<b>MapEditor</b> of GZH Neo<br>Map editor for Command &amp; Conquer Generals and Zero Hour.<br><br>GZH Neo is a modified version of Command &amp; Conquer Generals / Zero Hour (Electronic Arts), licensed under the GNU GPL v3 or later.<br>Uses Qt %1 (LGPL v3).")
			.arg(QString::fromLatin1(qVersion())));
}

} // namespace MapEditorApp
