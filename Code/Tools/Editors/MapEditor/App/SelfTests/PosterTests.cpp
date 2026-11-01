/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// PosterTests.cpp -- the self test of MainWindowPoster.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include <QDir>
#include <QImage>
#include <algorithm>
#include <cstdlib>

namespace MapEditorApp
{

bool MainWindow::selfTestPoster(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "poster");
	if (!m_state.map || !m_viewport || !m_viewport->hasDevice() || m_viewMapSerial != m_mapSerial)
		return true;
	EngineClient &engine = EngineClient::instance();
	// The whole view once, frozen, for the comparison with the poster made smaller.
	engine.freezeTime(true);
	engine.setOverlay({}, {});
	for (int i = 0; i < 3; ++i)
		engine.render();
	const QString wholeFile = QDir(shotDir).filePath(QStringLiteral("poster_view.bmp"));
	m_viewport->captureFrame(wholeFile);
	engine.freezeTime(false);
	applyOverlay();
	const QImage whole = QImage(wholeFile).convertToFormat(QImage::Format_RGB32);

	const QString png = QDir(shotDir).filePath(QStringLiteral("poster.png"));
	QString error;
	const bool written = exportPoster(png, 2, false, &error);
	const QImage poster(png);
	step(written && !whole.isNull() && poster.width() == whole.width() * 2 && poster.height() == whole.height() * 2,
		QStringLiteral("a poster of twice the view, %1 x %2 pixels, picture poster.png %3").arg(poster.width()).arg(poster.height()).arg(error));
	// The four tiles fit together: the poster made smaller looks like the view.
	if (written && !whole.isNull())
	{
		const QImage small = poster.scaled(whole.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
		qint64 differing = 0;
		for (int y = 0; y < small.height(); ++y)
		{
			const QRgb *a = reinterpret_cast<const QRgb *>(whole.constScanLine(y));
			const QRgb *b = reinterpret_cast<const QRgb *>(small.constScanLine(y));
			for (int x = 0; x < small.width(); ++x)
				differing += (std::abs(qRed(a[x]) - qRed(b[x])) + std::abs(qGreen(a[x]) - qGreen(b[x])) + std::abs(qBlue(a[x]) - qBlue(b[x]))) > 60 ? 1 : 0;
		}
		const double share = static_cast<double>(differing) / (static_cast<double>(small.width()) * small.height());
		step(share < 0.05, QStringLiteral("the tiles fit together into the view's picture (%1 % of the pixels differ)").arg(share * 100.0, 0, 'f', 1));
	}
	return step.ok();
}

} // namespace MapEditorApp
