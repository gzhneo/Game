/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowPoster.cpp -- the 3D view as a poster: the camera's picture at a multiple of the view's
// size, with every Direct3D 11 effect of the chosen graphics level, as a PNG picture. The
// view draws the picture in tiles (a part of the camera's view plane each) with the animations
// standing still, and the tiles are put together.

#include "App/MainWindow.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdlib>

namespace MapEditorApp
{

bool MainWindow::renderPoster(int scale, bool withOverlay, QImage *out, QString *error)
{
	EngineClient &engine = EngineClient::instance();
	if (!m_state.map || !m_viewport || !m_viewport->hasDevice() || m_viewMapSerial != m_mapSerial)
	{
		*error = T("The poster needs the map in the 3D view.");
		return false;
	}
	scale = std::clamp(scale, 1, 8);
	QTemporaryDir temp;
	if (!temp.isValid())
	{
		*error = T("No folder for the tiles of the poster.");
		return false;
	}
	// The size of one view in pixels, from a frame as the view draws it.
	engine.freezeTime(true);
	if (!withOverlay)
	{
		engine.setOverlay({}, {});
		engine.setTerrainTint({}, 0, 0, 0);
	}
	const QString probe = QDir(temp.path()).filePath(QStringLiteral("probe.bmp"));
	engine.render();
	m_viewport->captureFrame(probe);
	const QSize view = QImage(probe).size();
	// Each tile is drawn with a margin that is cut off again: the filters of the graphics level
	// (sharpening, antialiasing) treat the edge of a frame differently, which would show as seams.
	const int margin = std::min(16, std::min(view.width(), view.height()) / 8);
	const int innerW = view.width() - 2 * margin;
	const int innerH = view.height() - 2 * margin;
	QImage poster;
	bool ok = !view.isEmpty() && innerW > 0 && innerH > 0;
	if (!ok)
		*error = T("A tile of the poster cannot be read: %1").arg(probe);
	else
		poster = QImage(view.width() * scale, view.height() * scale, QImage::Format_RGB32);
	const int cols = ok ? (poster.width() + innerW - 1) / innerW : 0;
	const int rows = ok ? (poster.height() + innerH - 1) / innerH : 0;
	for (int row = 0; row < rows && ok; ++row)
	{
		for (int col = 0; col < cols && ok; ++col)
		{
			// The tile's frame in poster pixels, as fractions of the whole picture.
			const double x0 = col * innerW - margin;
			const double y0 = row * innerH - margin;
			engine.setTile(static_cast<float>(x0 / poster.width()), static_cast<float>(y0 / poster.height()),
				static_cast<float>((x0 + view.width()) / poster.width()), static_cast<float>((y0 + view.height()) / poster.height()));
			// The shadow map and the scene copies follow the camera: a few frames settle them.
			for (int i = 0; i < 3; ++i)
				engine.render();
			const QString file = QDir(temp.path()).filePath(QStringLiteral("tile_%1_%2.bmp").arg(col).arg(row));
			m_viewport->captureFrame(file);
			const QImage tile = QImage(file).convertToFormat(QImage::Format_RGB32);
			if (tile.size() != view)
			{
				ok = false;
				*error = T("A tile of the poster cannot be read: %1").arg(file);
				break;
			}
			QPainter p(&poster);
			p.drawImage(QPoint(col * innerW, row * innerH), tile, QRect(margin, margin, innerW, innerH));
		}
	}
	engine.setTile(0.0f, 0.0f, 1.0f, 1.0f);
	engine.freezeTime(false);
	updateImpassableView();
	applyOverlay();
	if (ok)
		*out = poster;
	return ok;
}

bool MainWindow::exportPoster(const QString &path, int scale, bool withOverlay, QString *error)
{
	QImage poster;
	if (!renderPoster(scale, withOverlay, &poster, error))
		return false;
	if (!poster.save(path))
	{
		*error = T("The file cannot be written: %1").arg(path);
		return false;
	}
	return true;
}

void MainWindow::showPosterDialog()
{
	if (!m_viewport || !m_viewport->hasDevice())
	{
		toast(T("The poster needs the map in the 3D view."));
		return;
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("PosterDialog"));
	dialog.setWindowTitle(T("Poster of the 3D view"));
	auto *form = new QFormLayout(&dialog);
	const QSize view = m_viewport->size() * m_viewport->devicePixelRatioF();
	auto *size = new QComboBox(&dialog);
	for (int s = 2; s <= 6; ++s)
		size->addItem(T("%1 x %2 pixels (%3 times the view)").arg(view.width() * s).arg(view.height() * s).arg(s), s);
	size->setCurrentIndex(1);
	form->addRow(T("Size"), size);
	auto *overlay = new QCheckBox(T("With the markings of the view (waypoints, areas, selection, slopes)"), &dialog);
	form->addRow(QString(), overlay);
	auto *hint = label(T("The picture shows the camera's view with the effects of the graphics level (View \u203a Graphics)."), "Hint", &dialog);
	hint->setWordWrap(true);
	form->addRow(hint);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	form->addRow(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;
	const QString path = QFileDialog::getSaveFileName(this, T("Save the poster"), QString(), T("Pictures (*.png)"));
	if (path.isEmpty())
		return;
	QApplication::setOverrideCursor(Qt::WaitCursor);
	QString error;
	const bool ok = exportPoster(path, size->currentData().toInt(), overlay->isChecked(), &error);
	QApplication::restoreOverrideCursor();
	toast(ok ? T("Saved: %1").arg(path) : error);
}

} // namespace MapEditorApp
