/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// CatalogTests.cpp -- the self test of MainWindowCatalog.cpp (MapEditor --selftest).

#include "App/Common.h"
#include "App/MainWindow.h"
#include "App/MainWindowParts.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Icons.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/WaypointCommands.h"
#include <QApplication>
#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTreeWidget>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace MapEditorApp
{

bool MainWindow::selfTestCatalog(const std::string &templateName, int nearObject, const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "catalog");
	EngineClient &engine = EngineClient::instance();
	auto &objects = *m_state.map->map().objects;
	MapEditorCore::History &history = m_state.map->history;
	const std::size_t before = objects.size();
	const QString name = utf8(templateName);

	m_workMode = WorkMode::Objects;
	m_show3D = true;
	rebuild();
	QApplication::processEvents();
	QTreeWidget *tree = m_catalogTree;
	step(tree && !engine.catalog().empty(), QStringLiteral("catalog with %1 templates").arg(engine.catalog().size()));
	if (!tree)
		return false;
	auto *search = tree->parentWidget()->findChild<QLineEdit *>(QStringLiteral("CatalogSearch"));
	if (search)
		search->setText(name);
	QTreeWidgetItem *item = findTemplate(tree, name);
	step(search && item && !item->isHidden(), QStringLiteral("search finds %1").arg(name));
	if (!item)
		return false;
	tree->setCurrentItem(item);
	step(m_placeTemplate == name, QStringLiteral("choosing it arms placing"));
	// The preview shows the model: many pixels differ from the background.
	const QImage previewImage = m_catalogPreview ? m_catalogPreview->pixmap().toImage() : QImage();
	int differ = 0;
	for (int y = 0; y < previewImage.height(); ++y)
	{
		for (int x = 0; x < previewImage.width(); ++x)
		{
			const QColor c = previewImage.pixelColor(x, y);
			if (std::abs(c.red() - m_previewBackground.red()) + std::abs(c.green() - m_previewBackground.green()) + std::abs(c.blue() - m_previewBackground.blue()) > 24)
				++differ;
		}
	}
	const int all = std::max(1, previewImage.width() * previewImage.height());
	step(!previewImage.isNull() && differ * 50 > all, QStringLiteral("preview shows the model (%1 % of the picture)").arg(differ * 100 / all));
	const QString previewShot = QDir(shotDir).filePath(QStringLiteral("catalog_preview.png"));
	step(!previewImage.isNull() && previewImage.save(previewShot), QStringLiteral("preview picture %1").arg(previewShot));
	// Pictures must not leave files open: the C runtime has a few hundred streams for editor and engine.
	const int freeBefore = freeFileStreams();
	for (int i = 0; i < 10; ++i)
	{
		m_previewYaw += 0.3f;
		updateCatalogPreview();
	}
	const int freeAfter = freeFileStreams();
	step(freeAfter >= freeBefore, QStringLiteral("10 more pictures leave no file open (%1 free streams before, %2 after)").arg(freeBefore).arg(freeAfter));
	const float yawBefore = m_previewYaw;
	if (m_catalogPreview)
	{
		QLabel *label = m_catalogPreview;
		const QPointF c(label->width() * 0.5, label->height() * 0.5);
		QMouseEvent press(QEvent::MouseButtonPress, c, label->mapToGlobal(c), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(label, &press);
		QMouseEvent move(QEvent::MouseMove, c + QPointF(40.0, 0.0), label->mapToGlobal(c + QPointF(40.0, 0.0)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(label, &move);
		QMouseEvent release(QEvent::MouseButtonRelease, c + QPointF(40.0, 0.0), label->mapToGlobal(c + QPointF(40.0, 0.0)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(label, &release);
	}
	const QImage turned = m_catalogPreview ? m_catalogPreview->pixmap().toImage() : QImage();
	step(std::abs(m_previewYaw - yawBefore) > 0.1f && !turned.isNull() && turned != previewImage, QStringLiteral("dragging the preview turns the model"));
	m_previewYaw = yawBefore;
	const QString windowShot = QDir(shotDir).filePath(QStringLiteral("objects_mode.png"));
	step(grab().save(windowShot), QStringLiteral("window in the objects mode %1").arg(windowShot));

	// 3D: a click places, a drag places and aims.
	MePickResult info{};
	engine.objectInfo(nearObject, &info);
	MeCamera c{};
	c.targetX = info.x;
	c.targetY = info.y;
	c.targetZ = info.z;
	c.pitch = 1.45f;
	c.distance = 260.0f;
	engine.setCamera(c);
	const auto mouse = [this](QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
		QMouseEvent event(type, pos, m_viewport->mapToGlobal(pos), button, buttons, Qt::NoModifier);
		QApplication::sendEvent(m_viewport, &event);
	};
	const QPointF at = QPointF(m_viewport->width() * 0.5, m_viewport->height() * 0.5) + QPointF(50.0, 0.0);
	mouse(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
	mouse(QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
	bool placed = objects.size() == before + 1 && objects.back().templateName == templateName;
	const int first = static_cast<int>(before);
	MePickResult shown{};
	const bool drawn = placed && engine.objectInfo(first, &shown);
	step(placed && history.undoName() == "Place object" && m_state.map->selection.objects == std::vector<std::int32_t>{first},
		QStringLiteral("3D: click places it at %1, %2 and selects it")
			.arg(placed ? static_cast<double>(objects.back().x) : 0.0, 0, 'f', 0).arg(placed ? static_cast<double>(objects.back().y) : 0.0, 0, 'f', 0));
	step(drawn, QStringLiteral("3D: the view draws the new object"));
	step(placed && objects.back().properties.getAscii("originalOwner").value_or("") == NEUTRAL_TEAM && objects.back().properties.getAscii("uniqueID"),
		QStringLiteral("3D: neutral team and a unique ID"));
	mouse(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
	mouse(QEvent::MouseMove, at + QPointF(0.0, -20.0), Qt::NoButton, Qt::LeftButton);
	mouse(QEvent::MouseMove, at + QPointF(0.0, -60.0), Qt::NoButton, Qt::LeftButton);
	mouse(QEvent::MouseButtonRelease, at + QPointF(0.0, -60.0), Qt::LeftButton, Qt::NoButton);
	placed = objects.size() == before + 2;
	step(placed && std::abs(objects.back().angle - m_placeAngle) > 0.01f,
		QStringLiteral("3D: drag places a second one turned to %1 degrees").arg(placed ? static_cast<double>(objects.back().angle) * 57.29577951308232 : 0.0, 0, 'f', 0));
	undo();
	undo();
	step(objects.size() == before && !history.isModified(), QStringLiteral("3D: undo both"));

	// 2D overview: a click places.
	m_show3D = false;
	rebuild();
	QApplication::processEvents();
	if (MapOverview *view = m_overview)
	{
		view->fit();
		const QPointF p = view->worldToWidget(info.x + 40.0f, info.y);
		QMouseEvent press(QEvent::MouseButtonPress, p, view->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(view, &press);
		QMouseEvent release(QEvent::MouseButtonRelease, p, view->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(view, &release);
		placed = objects.size() == before + 1;
		step(placed && std::abs(objects.back().x - (info.x + 40.0f)) < 20.0f && std::abs(objects.back().y - info.y) < 20.0f,
			QStringLiteral("2D: click places it"));
		undo();
		step(objects.size() == before && !history.isModified(), QStringLiteral("2D: undo"));
		QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
		QApplication::sendEvent(view, &esc);
		step(m_placeTemplate.isEmpty(), QStringLiteral("2D: Esc ends placing"));
	}
	else
		step(false, QStringLiteral("2D: no overview"));

	m_catalogFilter.clear();
	m_workMode = WorkMode::View;
	m_show3D = true;
	rebuild();
	QApplication::processEvents();
	return step.ok();
}

} // namespace MapEditorApp
