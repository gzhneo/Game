/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowCamera.cpp -- camera, ruler and preview picture, after the old WorldBuilder's
// CameraOptions (pitch, reset, centre on the selection, a waypoint where the camera looks), RulerTool
// (a length or a circle) and MapPreview (the .tga next to the map, written on every save).

#include "App/Common.h"
#include "App/MainWindow.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/MapPreview.h"
#include "Core/WaypointCommands.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

namespace MapEditorApp
{

// ------------------------------------------------------------------------------------------------
// Camera

void MainWindow::setCameraYaw(float degrees)
{
	EngineClient &engine = EngineClient::instance();
	if (!m_viewport || !m_viewport->hasDevice())
		return;
	MeCamera camera = engine.camera();
	camera.yaw = degrees * PI / 180.0f;
	engine.setCamera(camera);
	m_viewport->update();
	refreshCameraWindow();
}

void MainWindow::centerOnSelection()
{
	if (!m_state.map || !m_state.map->map().objects || m_state.map->selection.empty())
	{
		toast(T("Select something to centre on."));
		return;
	}
	float x = 0.0f, y = 0.0f;
	for (std::int32_t index : m_state.map->selection.objects)
	{
		const MapModel::MapObject &o = (*m_state.map->map().objects)[static_cast<std::size_t>(index)];
		x += o.x;
		y += o.y;
	}
	x /= static_cast<float>(m_state.map->selection.objects.size());
	y /= static_cast<float>(m_state.map->selection.objects.size());
	centerOn(x, y);
}

void MainWindow::waypointAtCamera()
{
	if (!m_state.map || !m_viewport || !m_viewport->hasDevice())
	{
		toast(T("The 3D view is needed for this."));
		return;
	}
	const MeCamera camera = EngineClient::instance().camera();
	placeWaypoint(camera.targetX, camera.targetY, -1, false);
}

void MainWindow::showCameraWindow()
{
	if (!m_cameraWindow)
	{
		m_cameraWindow = new QDialog(this);
		m_cameraWindow->setObjectName(QStringLiteral("CameraWindow"));
		m_cameraWindow->setWindowTitle(T("Camera"));
	}
	refreshCameraWindow();
	m_cameraWindow->show();
	m_cameraWindow->raise();
}

void MainWindow::refreshCameraWindow()
{
	if (!m_cameraWindow)
		return;
	QDialog *window = m_cameraWindow;
	for (QWidget *child : window->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
	{
		child->hide();
		child->deleteLater();
	}
	delete window->layout();
	auto *col = new QVBoxLayout(window);
	const bool view = m_viewport && m_viewport->hasDevice() && m_state.map;
	if (!view)
	{
		col->addWidget(label(T("The camera belongs to the 3D view."), "Hint"));
		return;
	}
	const MeCamera camera = EngineClient::instance().camera();
	auto *form = new QFormLayout;
	// A field of the camera in degrees or units; a change moves the camera at once.
	const auto field = [&](const QString &text, const char *name, int minimum, int maximum, float value, const QString &suffix,
		const std::function<void(MeCamera &, int)> &set) {
		auto *spin = new QSpinBox;
		spin->setObjectName(QString::fromLatin1(name));
		spin->setRange(minimum, maximum);
		spin->setSuffix(suffix);
		spin->setValue(static_cast<int>(std::lround(value)));
		connect(spin, &QSpinBox::valueChanged, this, [this, set](int v) {
			EngineClient &engine = EngineClient::instance();
			MeCamera c = engine.camera();
			set(c, v);
			engine.setCamera(c);
			if (m_viewport)
				m_viewport->update();
		});
		form->addRow(text, spin);
	};
	field(T("Pitch"), "CameraPitch", 5, 89, camera.pitch * 180.0f / PI, QStringLiteral("\u00b0"), [](MeCamera &c, int v) { c.pitch = static_cast<float>(v) * PI / 180.0f; });
	field(T("Direction"), "CameraYaw", -180, 180, std::remainder(camera.yaw * 180.0f / PI, 360.0f), QStringLiteral("\u00b0"),
		[](MeCamera &c, int v) { c.yaw = static_cast<float>(v) * PI / 180.0f; });
	field(T("Distance"), "CameraDistance", 50, 5000, camera.distance, QString(), [](MeCamera &c, int v) { c.distance = static_cast<float>(v); });
	col->addLayout(form);
	col->addWidget(label(T("Looks at %1, %2 (cell %3, %4)").arg(static_cast<double>(camera.targetX), 0, 'f', 0).arg(static_cast<double>(camera.targetY), 0, 'f', 0)
		.arg(std::lround(camera.targetX / CELL)).arg(std::lround(camera.targetY / CELL)), "Hint"));
	auto *row = new QHBoxLayout;
	const auto button = [&](const QString &text, const char *name, const std::function<void()> &action) {
		auto *b = new QPushButton(text);
		b->setObjectName(QString::fromLatin1(name));
		connect(b, &QPushButton::clicked, this, action);
		row->addWidget(b);
	};
	button(T("Start camera"), "CameraReset", [this] {
		EngineClient::instance().resetCamera();
		m_viewport->update();
		refreshCameraWindow();
	});
	button(T("Centre on selection"), "CameraCenter", [this] { centerOnSelection(); });
	button(T("Waypoint here"), "CameraWaypoint", [this] { waypointAtCamera(); });
	col->addLayout(row);

	// Bookmarks of this map: a click goes there, the cross forgets it.
	col->addWidget(label(T("BOOKMARKS"), "Section"));
	const std::vector<CameraBookmark> marks = m_state.bookmarks(m_state.map->path);
	for (std::size_t i = 0; i < marks.size(); ++i)
	{
		auto *line = new QHBoxLayout;
		auto *go = new QPushButton(marks[i].name);
		go->setObjectName(QStringLiteral("CameraBookmark"));
		const int index = static_cast<int>(i);
		connect(go, &QPushButton::clicked, this, [this, index] { goToBookmark(index); });
		auto *forget = new QPushButton(QStringLiteral("\u00d7"));
		forget->setObjectName(QStringLiteral("Ghost"));
		forget->setToolTip(T("Delete bookmark"));
		connect(forget, &QPushButton::clicked, this, [this, index] {
			std::vector<CameraBookmark> list = m_state.bookmarks(m_state.map->path);
			if (index < static_cast<int>(list.size()))
				list.erase(list.begin() + index);
			m_state.setBookmarks(m_state.map->path, list);
			QTimer::singleShot(0, this, [this] { refreshCameraWindow(); });
		});
		line->addWidget(go, 1);
		line->addWidget(forget);
		col->addLayout(line);
	}
	auto *add = new QPushButton(T("Save this view"));
	add->setObjectName(QStringLiteral("CameraBookmarkAdd"));
	connect(add, &QPushButton::clicked, this, [this] { addBookmark(); });
	col->addWidget(add);
}

// ------------------------------------------------------------------------------------------------
// Ruler

bool MainWindow::rulerTool() const
{
	return m_placeTemplate == QLatin1String(RULER_TOOL);
}

QString MainWindow::rulerText() const
{
	const float length = std::hypot(m_ruler.x2 - m_ruler.x1, m_ruler.y2 - m_ruler.y1);
	if (m_ruler.circle)
		return T("Radius %1, diameter %2 (%3 cells)").arg(static_cast<double>(length), 0, 'f', 1).arg(static_cast<double>(length * 2.0f), 0, 'f', 1)
			.arg(static_cast<double>(length * 2.0f / CELL), 0, 'f', 1);
	return T("Length %1 (%2 cells)").arg(static_cast<double>(length), 0, 'f', 1).arg(static_cast<double>(length / CELL), 0, 'f', 1);
}

std::vector<float> MainWindow::rulerLines() const
{
	std::vector<float> lines;
	if (!m_ruler.valid)
		return lines;
	const Ruler &r = m_ruler;
	if (!r.circle)
		return {r.x1, r.y1, r.x2, r.y2};
	const float radius = std::hypot(r.x2 - r.x1, r.y2 - r.y1);
	constexpr int SEGMENTS = 48;
	for (int i = 0; i < SEGMENTS; ++i)
	{
		const float a = static_cast<float>(i) * 2.0f * PI / SEGMENTS;
		const float b = static_cast<float>(i + 1) * 2.0f * PI / SEGMENTS;
		lines.insert(lines.end(), {r.x1 + std::cos(a) * radius, r.y1 + std::sin(a) * radius, r.x1 + std::cos(b) * radius, r.y1 + std::sin(b) * radius});
	}
	lines.insert(lines.end(), {r.x1, r.y1, r.x2, r.y2});
	return lines;
}

void MainWindow::rulerToolEvent(const ToolEvent &e)
{
	switch (e.kind)
	{
	case ToolEvent::Kind::Press:
		m_ruler.x1 = m_ruler.x2 = e.x;
		m_ruler.y1 = m_ruler.y2 = e.y;
		m_ruler.valid = true;
		m_ruler.dragging = true;
		break;
	case ToolEvent::Kind::Move:
		if (!m_ruler.dragging)
			return;
		m_ruler.x2 = e.x;
		m_ruler.y2 = e.y;
		break;
	case ToolEvent::Kind::Release:
		m_ruler.dragging = false;
		fillSelectionPanel();
		break;
	case ToolEvent::Kind::DoubleClick:
		return;
	}
	if (m_statusRight)
		m_statusRight->setText(rulerText());
	if (auto *readout = findChild<QLabel *>(QStringLiteral("RulerReadout")))
		readout->setText(rulerText());
	if (m_overview)
		m_overview->setToolLines(toolLines());
	applyOverlay();
}

void MainWindow::addRulerEditors(QVBoxLayout *box)
{
	box->addWidget(label(T("RULER"), "Section"));
	auto *modes = segmented({T("Length"), T("Circle")}, m_ruler.circle ? 1 : 0, [this](int i) {
		m_ruler.circle = i == 1;
		if (m_overview)
			m_overview->setToolLines(toolLines());
		applyOverlay();
		QTimer::singleShot(0, this, [this] { fillSelectionPanel(); });
	}, nullptr, {true, true});
	modes->setObjectName(QStringLiteral("RulerModes"));
	box->addWidget(modes);
	auto *readout = label(m_ruler.valid ? rulerText() : T("Drag to measure."), "Secondary");
	readout->setObjectName(QStringLiteral("RulerReadout"));
	box->addWidget(readout);
	if (m_ruler.circle && m_ruler.valid)
	{
		// The old ruler let the radius be typed in.
		auto *row = new QHBoxLayout;
		row->addWidget(label(T("Radius"), "Meta"));
		auto *radius = new QSpinBox;
		radius->setObjectName(QStringLiteral("RulerRadius"));
		radius->setRange(1, 20000);
		radius->setValue(static_cast<int>(std::lround(std::hypot(m_ruler.x2 - m_ruler.x1, m_ruler.y2 - m_ruler.y1))));
		connect(radius, &QSpinBox::valueChanged, this, [this, readout](int v) {
			const float length = std::max(std::hypot(m_ruler.x2 - m_ruler.x1, m_ruler.y2 - m_ruler.y1), 0.001f);
			const float scale = static_cast<float>(v) / length;
			m_ruler.x2 = m_ruler.x1 + (m_ruler.x2 - m_ruler.x1) * scale;
			m_ruler.y2 = m_ruler.y1 + (m_ruler.y2 - m_ruler.y1) * scale;
			if (length < 0.01f)
				m_ruler.x2 = m_ruler.x1 + static_cast<float>(v);
			readout->setText(rulerText());
			if (m_overview)
				m_overview->setToolLines(toolLines());
			applyOverlay();
		});
		row->addWidget(radius, 1);
		box->addLayout(row);
	}
	auto *hint = label(T("Drag to measure a length, or a circle around the point where the drag starts. The ruler changes nothing in the map."), "Hint");
	hint->setWordWrap(true);
	box->addWidget(hint);
}

// ------------------------------------------------------------------------------------------------
// Preview picture

std::vector<std::uint32_t> MainWindow::previewPixels() const
{
	MapEditorCore::ClassColours colours;
	const MapModel::Map &map = m_state.map->map();
	EngineClient &engine = EngineClient::instance();
	if (map.blendTiles && engine.isStarted())
	{
		// Each texture's colour: the average of a small picture of one of its tiles.
		for (const MapModel::TextureClass &c : map.blendTiles->textureClasses)
		{
			const QImage swatch = engine.terrainPreview(QString::fromStdString(c.name), 8);
			std::array<float, 3> sum{0.5f, 0.48f, 0.4f};
			if (!swatch.isNull())
			{
				sum = {0.0f, 0.0f, 0.0f};
				for (int y = 0; y < swatch.height(); ++y)
				{
					for (int x = 0; x < swatch.width(); ++x)
					{
						const QColor p = swatch.pixelColor(x, y);
						sum[0] += static_cast<float>(p.redF());
						sum[1] += static_cast<float>(p.greenF());
						sum[2] += static_cast<float>(p.blueF());
					}
				}
				for (float &v : sum)
					v /= static_cast<float>(swatch.width() * swatch.height());
			}
			colours.push_back(sum);
		}
	}
	return MapEditorCore::mapPreview(map, colours);
}

bool MainWindow::writePreview(const QString &mapPath)
{
	if (!m_state.map || !m_state.map->map().heightMap)
		return false;
	const std::vector<std::uint8_t> tga = MapEditorCore::previewTga(previewPixels());
	QFileInfo info(mapPath);
	QFile file(info.dir().filePath(info.completeBaseName() + QStringLiteral(".tga")));
	if (!file.open(QIODevice::WriteOnly))
		return false;
	return file.write(reinterpret_cast<const char *>(tga.data()), static_cast<qint64>(tga.size())) == static_cast<qint64>(tga.size());
}

} // namespace MapEditorApp
