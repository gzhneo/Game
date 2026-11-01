/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// main.cpp -- the map editor of GZH Neo.
//
//   MapEditor [file.map]
//   MapEditor --selftest <folder> [file.map] [--data <game data folder>]
//
// The self test draws every page in both themes and languages into PNG files without showing a
// window and without touching the user's settings, then exits: 0 if everything was drawn. A GUI
// program has no console on Windows, so the output also goes to selftest.log in the folder. With
// --data it also starts the engine (Zero Hour) and saves the 3D view of the map (map3d_*.png).

#include "App/AppState.h"
#include "App/DemoMap.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace MapEditorApp;

namespace
{

// What is wrong with a frame of the 3D view, empty if nothing: the middle of the picture (where the map
// lies in both test views) must neither be mostly black (terrain without textures or light), mostly
// magenta (the renderer's colour for a missing texture) nor one flat colour.
QString frameProblem(const QImage &frame)
{
	if (frame.isNull())
		return QStringLiteral("no picture");
	const QImage image = frame.convertToFormat(QImage::Format_RGB32);
	const QRect middle(image.width() / 5, image.height() / 5, image.width() * 3 / 5, image.height() * 3 / 5);
	qint64 total = 0;
	qint64 black = 0;
	qint64 magenta = 0;
	double sum = 0.0;
	double sumSquares = 0.0;
	for (int y = middle.top(); y <= middle.bottom(); ++y)
	{
		const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
		for (int x = middle.left(); x <= middle.right(); ++x)
		{
			const int r = qRed(row[x]);
			const int g = qGreen(row[x]);
			const int b = qBlue(row[x]);
			const double grey = 0.299 * r + 0.587 * g + 0.114 * b;
			++total;
			black += (r < 12 && g < 12 && b < 12) ? 1 : 0;
			magenta += (r > 200 && g < 60 && b > 200) ? 1 : 0;
			sum += grey;
			sumSquares += grey * grey;
		}
	}
	if (total == 0)
		return QStringLiteral("empty picture");
	const double mean = sum / static_cast<double>(total);
	const double spread = std::sqrt(std::max(0.0, sumSquares / static_cast<double>(total) - mean * mean));
	const auto percent = [total](qint64 n) { return static_cast<int>(n * 100 / total); };
	if (percent(black) > 40)
		return QStringLiteral("%1 % of the middle is black").arg(percent(black));
	if (percent(magenta) > 5)
		return QStringLiteral("%1 % of the middle is magenta (missing textures)").arg(percent(magenta));
	if (spread < 4.0)
		return QStringLiteral("the middle is one flat colour");
	return QString();
}

int selfTest(QApplication &app, const QString &folder, const QString &mapFile, const QString &dataFolder)
{
	QDir().mkpath(folder);
	QFile log(QDir(folder).filePath(QStringLiteral("selftest.log")));
	const bool logOpen = log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
	auto say = [&](const QString &line) {
		std::printf("%s\n", qPrintable(line));
		if (logOpen)
			log.write((line + QLatin1Char('\n')).toUtf8());
	};

	AppState state(QDir(folder).filePath(QStringLiteral("selftest.ini")));
	state.recent.clear();
	state.game = Game::ZeroHour;
	// The self test saves on its own; the timer would write into the user's autosave folder.
	state.autosave = false;
	int written = 0;
	int failed = 0;
	auto shoot = [&](MainWindow &window, const QString &name) {
		app.processEvents();
		QTimer::singleShot(0, [] {});
		app.processEvents();
		const QImage image = window.grab().toImage();
		const QString file = QDir(folder).filePath(name + QStringLiteral(".png"));
		if (!image.isNull() && image.save(file))
			++written;
		else
			++failed;
		say(QStringLiteral("%1 %2").arg(image.isNull() ? QStringLiteral("FAIL") : QStringLiteral("ok  "), file));
	};

	for (Language language : {Language::German, Language::English})
	{
		for (ThemeKind theme : {ThemeKind::Dark, ThemeKind::Light})
		{
			state.language = language;
			state.theme = theme;
			state.map.reset();
			const QString suffix = QStringLiteral("_%1_%2").arg(theme == ThemeKind::Dark ? QStringLiteral("dark") : QStringLiteral("light"),
				language == Language::German ? QStringLiteral("de") : QStringLiteral("en"));
			MainWindow window(state, app);
			window.setAttribute(Qt::WA_DontShowOnScreen);
			window.resize(1440, 900);
			window.show();
			say(QStringLiteral("info free file streams: %1").arg(MainWindow::freeFileStreams()));
			shoot(window, QStringLiteral("start") + suffix);
			window.showPage(MainWindow::Page::Settings);
			shoot(window, QStringLiteral("settings") + suffix);
			QString error;
			if (!mapFile.isEmpty())
			{
				if (!window.openMapFile(mapFile, &error))
				{
					say(QStringLiteral("FAIL open %1: %2 (file %3, %4 free file streams)").arg(mapFile, error,
						QFileInfo::exists(mapFile) ? QStringLiteral("exists") : QStringLiteral("missing")).arg(MainWindow::freeFileStreams()));
					++failed;
				}
			}
			else
			{
				window.showMapModel(makeDemoMap(), QStringLiteral("Demo"));
			}
			shoot(window, QStringLiteral("map") + suffix);
			// The engine starts once per process; the 3D view is tested in the first round.
			if (!dataFolder.isEmpty() && language == Language::German && theme == ThemeKind::Dark)
			{
				state.setDataFolder(Game::ZeroHour, dataFolder);
				window.setEngineLog(QDir(folder).filePath(QStringLiteral("engine.log")));
				// Start camera (as the player sees the map) and the whole map from above.
				QImage lastFrame;
				for (const bool wholeMap : {false, true})
				{
					const QString name = (wholeMap ? QStringLiteral("map3d_whole") : QStringLiteral("map3d")) + suffix;
					const QString bmp = QDir(folder).filePath(name + QStringLiteral(".bmp"));
					QString problem;
					if (!window.capture3D(bmp, &problem, wholeMap))
					{
						++failed;
						say(QStringLiteral("FAIL 3D view: %1").arg(problem));
						break;
					}
					app.processEvents();
					const QImage frame(bmp);
					const QString png = QDir(folder).filePath(name + QStringLiteral(".png"));
					if (!frame.isNull() && frame.save(png))
					{
						++written;
						lastFrame = frame;
						QFile::remove(bmp);
						say(QStringLiteral("ok   %1 (%2 x %3)").arg(png).arg(frame.width()).arg(frame.height()));
						const QString content = frameProblem(frame);
						if (!content.isEmpty())
							++failed;
						say(content.isEmpty() ? QStringLiteral("ok   picture content of %1").arg(name) : QStringLiteral("FAIL picture content of %1: %2").arg(name, content));
					}
					else
					{
						++failed;
						say(QStringLiteral("FAIL 3D frame not written: %1").arg(bmp));
					}
				}
				// Editing: pick, move, undo, redo.
				{
					const QString bmp = QDir(folder).filePath(QStringLiteral("map3d_edit") + suffix + QStringLiteral(".bmp"));
					QStringList lines;
					if (!window.selfTestEdit(bmp, lines))
						++failed;
					for (const QString &line : lines)
						say(line);
					app.processEvents();
					const QImage frame(bmp);
					const QString png = QDir(folder).filePath(QStringLiteral("map3d_edit") + suffix + QStringLiteral(".png"));
					if (!frame.isNull() && frame.save(png))
					{
						++written;
						QFile::remove(bmp);
					}
					else
					{
						++failed;
						say(QStringLiteral("FAIL edit frame not written: %1").arg(bmp));
					}
				}
				// A window grab shows the native 3D window empty; the last frame is painted into it (the
				// painter works in logical pixels, like the viewport geometry).
				app.processEvents();
				QImage composed = window.grab().toImage();
				if (!composed.isNull() && !lastFrame.isNull())
				{
					QPainter painter(&composed);
					const QRect r = window.viewportRect();
					painter.drawImage(QRectF(r.x(), r.y(), r.width(), r.height()), lastFrame);
				}
				const QString windowShot = QDir(folder).filePath(QStringLiteral("map3d_window") + suffix + QStringLiteral(".png"));
				if (!composed.isNull() && composed.save(windowShot))
					++written;
				else
					++failed;
				for (const QString &line : EngineClient::instance().report().split(QLatin1Char('\n'), Qt::SkipEmptyParts))
					say(QStringLiteral("engine: ") + line);
			}
			window.close();
		}
	}
	say(QStringLiteral("German texts: %1").arg(germanEntryCount()));
	say(QStringLiteral("%1 images, %2 failed").arg(written).arg(failed));
	return failed == 0 && written > 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	setTranslationFile(QStringLiteral(":/mapeditor/de.lang"));
	QApplication::setApplicationName(QStringLiteral("MapEditor"));
	QApplication::setOrganizationName(QStringLiteral("GZH Neo"));

	const QStringList args = QApplication::arguments();
	const qsizetype st = args.indexOf(QStringLiteral("--selftest"));
	if (st >= 0)
	{
		if (st + 1 >= args.size())
		{
			std::printf("usage: MapEditor --selftest <folder> [file.map] [--data <folder>]\n");
			return 2;
		}
		const qsizetype dataAt = args.indexOf(QStringLiteral("--data"));
		const QString dataFolder = dataAt >= 0 && dataAt + 1 < args.size() ? args[dataAt + 1] : QString();
		const QString mapFile = st + 2 < args.size() && !args[st + 2].startsWith(QStringLiteral("--")) ? args[st + 2] : QString();
		return selfTest(app, args[st + 1], mapFile, dataFolder);
	}

	AppState state;
	MainWindow window(state, app);
	window.show();
	if (args.size() > 1 && QFileInfo(args.last()).isFile())
		window.openMapFile(args.last());
	return QApplication::exec();
}
