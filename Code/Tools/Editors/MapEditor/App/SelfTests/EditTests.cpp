/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EditTests.cpp -- the self test of MainWindowEdit.cpp (MapEditor --selftest).

#include "App/Common.h"
#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/RoadCommands.h"
#include "Core/LayerCommands.h"
#include "Core/SelectionTools.h"
#include "Core/WaypointCommands.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QTreeWidget>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace MapEditorApp
{

bool MainWindow::selfTestEdit(const QString &bmpPath, QStringList &report)
{
	if (!m_state.map || !m_state.map->map().objects || !m_viewport->hasDevice())
	{
		report << QStringLiteral("FAIL edit: no map or no 3D view");
		return false;
	}
	EngineClient &engine = EngineClient::instance();
	auto &objects = *m_state.map->map().objects;
	// Two objects the view knows, models first (a sound or a waypoint is only a small handle).
	std::vector<int> picks;
	for (const int wanted : {ME_OBJECT_MODEL, ME_OBJECT_TREE, ME_OBJECT_MARKER})
	{
		for (int i = 0; i < static_cast<int>(objects.size()) && picks.size() < 2; ++i)
		{
			MePickResult info{};
			if (engine.objectInfo(i, &info) && info.kind == wanted && std::find(picks.begin(), picks.end(), i) == picks.end())
				picks.push_back(i);
		}
	}
	if (picks.size() < 2)
	{
		report << QStringLiteral("FAIL edit: fewer than two objects in the view");
		return false;
	}
	const int index = picks[0];
	const int second = picks[1];
	MapModel::MapObject &o = objects[static_cast<std::size_t>(index)];
	MapModel::MapObject &o2 = objects[static_cast<std::size_t>(second)];
	const float startX = o.x;
	const float startY = o.y;
	const float start2X = o2.x;
	MapEditorCore::History &history = m_state.map->history;
	MapEditorCore::Selection &sel = m_state.map->selection;

	// Nearly straight down onto an object at its ground height, so the centre of the view hits it.
	const auto lookAt = [&engine](int object) {
		MePickResult info{};
		engine.objectInfo(object, &info);
		MeCamera c{};
		c.targetX = info.x;
		c.targetY = info.y;
		c.targetZ = info.z;
		c.pitch = 1.45f;
		c.distance = 260.0f;
		engine.setCamera(c);
	};
	TestSteps step(report, "edit");
	const QPointF center(m_viewport->width() * 0.5, m_viewport->height() * 0.5);
	const auto mouse = [this](QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
		QMouseEvent event(type, pos, m_viewport->mapToGlobal(pos), button, buttons, mods);
		QApplication::sendEvent(m_viewport, &event);
	};
	const auto click = [&](QPointF pos, Qt::KeyboardModifiers mods) {
		mouse(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton, mods);
		mouse(QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton, mods);
	};

	// Click picks the object.
	lookAt(index);
	MePickResult hit{};
	bool passed = m_viewport->pickCenter(&hit) && hit.object == index;
	step(passed, QStringLiteral("pick object %1 (%2) -> %3, radius %4").arg(index).arg(utf8(o.templateName)).arg(hit.object).arg(static_cast<double>(hit.radius), 0, 'f', 1));
	click(center, Qt::NoModifier);
	step(sel.objects == std::vector<std::int32_t>{index}, QStringLiteral("click selects it"));

	// Drag with the mouse: one step in the history, undo restores.
	mouse(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	mouse(QEvent::MouseMove, center + QPointF(20.0, 0.0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
	mouse(QEvent::MouseMove, center + QPointF(60.0, 0.0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
	mouse(QEvent::MouseButtonRelease, center + QPointF(60.0, 0.0), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	passed = o.x > startX + 1.0f && history.canUndo() && history.undoName() == "Move object";
	step(passed, QStringLiteral("mouse drag moves by %1, %2").arg(static_cast<double>(o.x - startX), 0, 'f', 1).arg(static_cast<double>(o.y - startY), 0, 'f', 1));
	undo();
	step(o.x == startX && o.y == startY && !history.isModified(), QStringLiteral("undo the drag"));

	// Ctrl+click adds the second object; dragging moves both as one step.
	lookAt(second);
	click(center, Qt::ControlModifier);
	step(sel.objects.size() == 2 && sel.contains(index) && sel.contains(second), QStringLiteral("Ctrl+click adds object %1").arg(second));
	dragSelection(40.0f, 20.0f);
	endDragSelection(false);
	step(o.x == startX + 40.0f && o2.x == start2X + 40.0f && history.undoName() == "Move objects", QStringLiteral("both moved by 40, 20 in one step"));
	lookAt(second);
	// The frame also shows a selection rectangle (device pixels of the view).
	const int w = static_cast<int>(m_viewport->width() * m_viewport->devicePixelRatioF());
	const int h = static_cast<int>(m_viewport->height() * m_viewport->devicePixelRatioF());
	engine.setMarquee(w / 8, h / 8, w * 3 / 8, h * 3 / 8);
	step(m_viewport->captureFrame(bmpPath), QStringLiteral("frame with two selected, moved objects and a selection rectangle"));
	engine.setMarquee(0, 0, 0, 0);
	undo();
	step(o.x == startX && o2.x == start2X && !history.isModified(), QStringLiteral("undo moves both back"));

	// Escape a drag: nothing recorded.
	dragSelection(10.0f, 0.0f);
	endDragSelection(true);
	step(o.x == startX && !history.canUndo(), QStringLiteral("cancelled drag leaves no step"));

	// Turning with the mouse: Shift+drag around the object, a quarter turn to the left.
	lookAt(index);
	sel.set(index);
	selectionChanged();
	const float angle0 = o.angle;
	mouse(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
	mouse(QEvent::MouseMove, center + QPointF(60.0, 0.0), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
	mouse(QEvent::MouseMove, center + QPointF(0.0, -60.0), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
	mouse(QEvent::MouseButtonRelease, center + QPointF(0.0, -60.0), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
	const double turned = std::remainder(static_cast<double>(o.angle - angle0), 6.283185307179586) * RAD_TO_DEG;
	step(std::abs(turned - 90.0) < 20.0 && o.x == startX && o.y == startY && history.undoName() == "Rotate object",
		QStringLiteral("Shift+drag turns the object by %1 degrees").arg(turned, 0, 'f', 1));
	undo();
	step(o.angle == angle0 && !history.isModified(), QStringLiteral("undo the turn"));

	// Two objects turn around their centre; Ctrl snaps to 15 degrees (50 becomes 45).
	sel.set(index);
	sel.add(second);
	selectionChanged();
	const float cx = (o.x + o2.x) * 0.5f;
	const float cy = (o.y + o2.y) * 0.5f;
	const float gap = std::hypot(o.x - o2.x, o.y - o2.y);
	const float angle2 = o2.angle;
	const float a50 = static_cast<float>(50.0 / RAD_TO_DEG);
	rotateSelection(cx + 100.0f, cy, cx + 100.0f * std::cos(a50), cy + 100.0f * std::sin(a50), true);
	endRotateSelection(false);
	const double turned2 = std::remainder(static_cast<double>(o2.angle - angle2), 6.283185307179586) * RAD_TO_DEG;
	step(std::abs(turned2 - 45.0) < 0.01 && std::abs(std::hypot(o.x - o2.x, o.y - o2.y) - gap) < 0.01f
		&& std::abs((o.x + o2.x) * 0.5f - cx) < 0.01f && history.undoName() == "Rotate objects",
		QStringLiteral("two objects turn by %1 degrees around their centre").arg(turned2, 0, 'f', 1));
	undo();
	step(o.x == startX && o2.angle == angle2 && !history.isModified(), QStringLiteral("undo turns both back"));

	// The panel edits the single object: angle as one step.
	sel.set(index);
	selectionChanged();
	step(m_selectionFields[3] != nullptr, QStringLiteral("panel shows the fields of one object"));
	const float angle = o.angle;
	editSelected(3, std::fmod(static_cast<double>(angle) * RAD_TO_DEG + 90.0, 360.0));
	step(std::abs(std::remainder(o.angle - angle - 1.5707963f, 6.2831853f)) < 1.0e-3f && history.undoName() == "Rotate object", QStringLiteral("panel turns the object by 90 degrees"));
	undo();
	step(o.angle == angle && !history.isModified(), QStringLiteral("undo the turn"));

	// Delete: the model shrinks, the view renumbers its objects; undo puts the object back at its
	// index and selects it. References into the object list are not used after this point.
	{
		const std::size_t n = objects.size();
		const std::string name = objects[static_cast<std::size_t>(index)].templateName;
		const float x = objects[static_cast<std::size_t>(index)].x;
		sel.set(index);
		selectionChanged();
		deleteSelection();
		MePickResult last{};
		const bool renumbered = !engine.objectInfo(static_cast<int>(n) - 1, nullptr) && engine.objectInfo(static_cast<int>(n) - 2, &last);
		step(objects.size() == n - 1 && sel.empty() && history.undoName() == "Delete object" && renumbered,
			QStringLiteral("delete object %1 (%2), view renumbered").arg(index).arg(utf8(name)));
		undo();
		MePickResult back{};
		const bool restored = engine.objectInfo(index, &back) && std::abs(back.x - x) < 0.01f && engine.objectInfo(static_cast<int>(n) - 1, nullptr);
		step(objects.size() == n && objects[static_cast<std::size_t>(index)].templateName == name && objects[static_cast<std::size_t>(index)].x == x
			&& sel.objects == std::vector<std::int32_t>{index} && restored && !history.isModified(),
			QStringLiteral("undo puts it back and selects it"));
		sel.set(index);
		sel.add(second);
		selectionChanged();
		deleteSelection();
		step(objects.size() == n - 2 && history.undoName() == "Delete objects", QStringLiteral("delete two objects"));
		undo();
		step(objects.size() == n && sel.objects.size() == 2 && !history.isModified(), QStringLiteral("undo puts both back"));
	}

	// Copy and paste (away from the cursor: 30 units beside the originals), duplicate, cut. The
	// user's clipboard is put back afterwards.
	{
		QClipboard *system = QGuiApplication::clipboard();
		auto userClipboard = std::make_unique<QMimeData>();
		if (const QMimeData *before = system->mimeData())
			for (const QString &format : before->formats())
				userClipboard->setData(format, before->data(format));
		m_hoverValid = false;
		const std::size_t n = objects.size();
		const float x = objects[static_cast<std::size_t>(index)].x;
		sel.set(index);
		sel.add(second);
		selectionChanged();
		copySelection();
		paste();
		const bool inView = engine.objectInfo(static_cast<int>(n) + 1, nullptr);
		step(objects.size() == n + 2 && sel.objects == (std::vector<std::int32_t>{static_cast<std::int32_t>(n), static_cast<std::int32_t>(n) + 1})
			&& objects[n].x == x + 30.0f && objects[n].templateName == objects[static_cast<std::size_t>(index)].templateName
			&& inView && history.undoName() == "Paste", QStringLiteral("copy and paste two objects, the copies are selected and in the view"));
		undo();
		step(objects.size() == n && !history.isModified(), QStringLiteral("undo the paste"));
		// The copy also went to the system clipboard; paste takes it from there without the editor's own.
		// Another program holding the clipboard is no fault of the editor: noted, not failed.
		const bool written = toSystemClipboard(m_clipboard);
		const bool owned = system->ownsClipboard();
		const QMimeData *shared = system->mimeData();
		const QStringList formats = shared ? shared->formats() : QStringList();
		const MapEditorCore::Clipboard own = m_clipboard;
		m_clipboard = {};
		paste();
		const bool pasted = objects.size() == n + 2 && objects[n].templateName == objects[static_cast<std::size_t>(index)].templateName;
		if (!written && !pasted)
			report << QStringLiteral("info edit: the system clipboard could not be set (busy or locked), paste from it not tested; formats there: %1")
				.arg(formats.join(QStringLiteral(", ")));
		else
			step(pasted, pasted ? QStringLiteral("the system clipboard holds the copies; paste takes them from there")
				: QStringLiteral("the system clipboard holds the copies; paste takes them from there (written %1, owned %2, formats: %3)")
					.arg(written).arg(owned).arg(formats.join(QStringLiteral(", "))));
		m_clipboard = own;
		if (pasted)
			undo();
		sel.set(index);
		selectionChanged();
		duplicateSelection();
		step(objects.size() == n + 1 && history.undoName() == "Duplicate" && sel.objects.size() == 1, QStringLiteral("duplicate"));
		undo();
		sel.set(index);
		selectionChanged();
		cutSelection();
		step(objects.size() == n - 1 && history.undoName() == "Cut" && m_clipboard.objects.size() == 1, QStringLiteral("cut"));
		paste();
		step(objects.size() == n && history.undoName() == "Paste", QStringLiteral("paste the cut object"));
		// With the cursor over the 3D view, the copy lands under it.
		const float y = objects[n - 1].y;
		m_hoverValid = true;
		m_hoverX = x + 100.0f;
		m_hoverY = y;
		paste();
		m_hoverValid = false;
		step(objects.size() == n + 1 && objects[n].x == x + 100.0f && objects[n].y == y, QStringLiteral("paste at the cursor"));
		undo();
		undo();
		undo();
		step(objects.size() == n && objects[static_cast<std::size_t>(index)].x == x && !history.isModified(), QStringLiteral("undo paste and cut, map unmodified"));
		if (userClipboard->formats().isEmpty())
			system->clear();
		else
			system->setMimeData(userClipboard.release());
	}

	// Properties: health of one object (the view rebuilds its objects), a switch on two, a value
	// typed into the list of all properties, a value that does not fit; all undone.
	{
		const auto &list = objects;
		// The objects before the edits: EA maps often carry these properties already, so undo must
		// bring back the old values, not remove them.
		const MapModel::MapObject before = list[static_cast<std::size_t>(index)];
		const MapModel::MapObject before2 = list[static_cast<std::size_t>(second)];
		sel.set(index);
		selectionChanged();
		const bool set = setSelectedProperty("objectInitialHealth", MapModel::PropertyValue(std::int32_t(50)), "Change health", false);
		step(set && list[static_cast<std::size_t>(index)].properties.getInt("objectInitialHealth") == 50
			&& history.undoName() == "Change health" && engine.objectInfo(index, nullptr), QStringLiteral("health 50 % on one object"));
		// The same value again is no step.
		step(!setSelectedProperty("objectInitialHealth", MapModel::PropertyValue(std::int32_t(50)), "Change health", false), QStringLiteral("unchanged value makes no step"));
		QTreeWidgetItem *item = nullptr;
		for (QTreeWidget *tree : m_selectionBox->findChildren<QTreeWidget *>())
		{
			const QList<QTreeWidgetItem *> found = tree->findItems(QStringLiteral("objectInitialHealth"), Qt::MatchExactly, 0);
			if (!found.isEmpty())
				item = found.first();
		}
		if (item)
			item->setText(1, QStringLiteral("75"));
		step(item && list[static_cast<std::size_t>(index)].properties.getInt("objectInitialHealth") == 75, QStringLiteral("value typed into the property list"));
		QApplication::processEvents();	// the old panel goes
		item = nullptr;
		for (QTreeWidget *tree : m_selectionBox->findChildren<QTreeWidget *>())
		{
			const QList<QTreeWidgetItem *> found = tree->findItems(QStringLiteral("objectInitialHealth"), Qt::MatchExactly, 0);
			if (!found.isEmpty())
				item = found.first();
		}
		if (item)
			item->setText(1, QStringLiteral("abc"));
		step(item && list[static_cast<std::size_t>(index)].properties.getInt("objectInitialHealth") == 75, QStringLiteral("a value that does not fit is refused"));
		undo();
		undo();
		step(list[static_cast<std::size_t>(index)] == before && !history.isModified(), QStringLiteral("undo both health changes"));
		sel.set(index);
		sel.add(second);
		selectionChanged();
		const bool flip = !before.properties.getBool("objectIndestructible").value_or(false);
		setSelectedProperty("objectIndestructible", MapModel::PropertyValue(flip), "Change property", false);
		step(list[static_cast<std::size_t>(index)].properties.getBool("objectIndestructible") == flip
			&& list[static_cast<std::size_t>(second)].properties.getBool("objectIndestructible") == flip && sel.objects.size() == 2,
			QStringLiteral("a switch on two objects at once"));
		undo();
		step(list[static_cast<std::size_t>(index)] == before && list[static_cast<std::size_t>(second)] == before2 && !history.isModified(),
			QStringLiteral("undo the switch"));
	}
	// Adding and removing a property in the list of one object.
	{
		sel.set(index);
		selectionChanged();
		QApplication::processEvents();
		auto &list = *m_state.map->map().objects;
		const MapModel::MapObject before = list[static_cast<std::size_t>(index)];
		QLineEdit *name = m_selectionBox ? m_selectionBox->findChild<QLineEdit *>(QStringLiteral("NewPropertyName")) : nullptr;
		QComboBox *type = m_selectionBox ? m_selectionBox->findChild<QComboBox *>(QStringLiteral("NewPropertyType")) : nullptr;
		QLineEdit *value = m_selectionBox ? m_selectionBox->findChild<QLineEdit *>(QStringLiteral("NewPropertyValue")) : nullptr;
		QPushButton *add = m_selectionBox ? m_selectionBox->findChild<QPushButton *>(QStringLiteral("NewPropertyAdd")) : nullptr;
		bool refused = false;
		if (name && type && value && add)
		{
			name->setText(QStringLiteral("editorSelfTest"));
			type->setCurrentIndex(1);
			value->setText(QStringLiteral("seven"));
			add->click();
			refused = !list[static_cast<std::size_t>(index)].properties.find("editorSelfTest");
			value->setText(QStringLiteral("7"));
			add->click();
		}
		step(refused && list[static_cast<std::size_t>(index)].properties.getInt("editorSelfTest") == 7 && history.undoName() == "Add property",
			QStringLiteral("a property added in the list (a wrong value refused)"));
		QApplication::processEvents();
		QTreeWidget *props = m_selectionBox ? m_selectionBox->findChild<QTreeWidget *>() : nullptr;
		QPushButton *remove = m_selectionBox ? m_selectionBox->findChild<QPushButton *>(QStringLiteral("RemoveProperty")) : nullptr;
		if (props && remove)
		{
			const QList<QTreeWidgetItem *> found = props->findItems(QStringLiteral("editorSelfTest"), Qt::MatchExactly, 0);
			if (!found.isEmpty())
				props->setCurrentItem(found.first());
			remove->click();
		}
		step(!list[static_cast<std::size_t>(index)].properties.find("editorSelfTest") && history.undoName() == "Remove property",
			QStringLiteral("the property removed again"));
		undo();
		undo();
		step(list[static_cast<std::size_t>(index)] == before && !history.isModified(), QStringLiteral("undo both"));
	}

	// Rectangle over the whole view and select all.
	engine.resetCamera();
	const std::vector<int> inView = engine.objectsInRect(0, 0, m_viewport->width() * 4, m_viewport->height() * 4);
	selectRect(inView, false);
	step(!inView.empty() && sel.objects.size() == inView.size(), QStringLiteral("rectangle selects %1 objects").arg(inView.size()));
	selectAll();
	step(sel.objects.size() >= inView.size(), QStringLiteral("select all: %1 objects").arg(sel.objects.size()));
	selectNone();
	step(sel.empty() && !history.isModified(), QStringLiteral("select none, map unmodified"));
	// The same in the 2D overview: click, drag, rectangle, Esc.
	m_show3D = false;
	rebuild();
	QApplication::processEvents();
	if (m_overview)
	{
		MapOverview *view = m_overview;
		view->fit();
		const MapModel::MapObject &target = objects[static_cast<std::size_t>(index)];
		const float x0 = target.x;
		const float y0 = target.y;
		const QPointF at = view->worldToWidget(x0, y0);
		const auto send = [view](QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
			QMouseEvent event(type, pos, view->mapToGlobal(pos), button, buttons, mods);
			QApplication::sendEvent(view, &event);
		};
		send(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		send(QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		step(sel.objects.size() == 1 && view->objectAt(at) == sel.objects[0], QStringLiteral("2D: click selects the object under the cursor"));
		const int picked = sel.objects.empty() ? -1 : sel.objects[0];
		const MapModel::MapObject *p = picked >= 0 ? &objects[static_cast<std::size_t>(picked)] : nullptr;
		const float px = p ? p->x : 0.0f;
		send(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		send(QEvent::MouseMove, at + QPointF(10.0, 0.0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		send(QEvent::MouseMove, at + QPointF(30.0, 0.0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		send(QEvent::MouseButtonRelease, at + QPointF(30.0, 0.0), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		step(p && p->x > px + 1.0f && history.undoName() == "Move object",
			QStringLiteral("2D: drag moves it by %1").arg(p ? static_cast<double>(p->x - px) : 0.0, 0, 'f', 1));
		undo();
		step(p && p->x == px && !history.isModified(), QStringLiteral("2D: undo the move"));
		send(QEvent::MouseButtonPress, at - QPointF(40.0, 40.0), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		send(QEvent::MouseMove, at + QPointF(40.0, 40.0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		send(QEvent::MouseButtonRelease, at + QPointF(40.0, 40.0), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		step(sel.contains(picked) && sel.objects.size() >= 1, QStringLiteral("2D: rectangle selects %1 objects").arg(sel.objects.size()));
		// Picture of the overview with the selection, next to the 3D frame.
		const QString shot = QFileInfo(bmpPath).dir().filePath(QStringLiteral("map2d_edit.png"));
		step(view->grab().save(shot), QStringLiteral("2D: picture %1").arg(shot));
		QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
		QApplication::sendEvent(view, &esc);
		step(sel.empty(), QStringLiteral("2D: Esc clears the selection"));
	}
	else
		step(false, QStringLiteral("2D: no overview"));
	m_show3D = true;
	rebuild();
	QApplication::processEvents();
	step.add(selfTestSound(index, report));
	step.add(selfTestCatalog(objects[static_cast<std::size_t>(index)].templateName, index, QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestWaypoints(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestAreas(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestRoads(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestPlacement(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestSelection(report));
	step.add(selfTestLayers(report));
	step.add(selfTestMapSettings(report));
	step.add(selfTestCamera(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestMapSize(report));
	step.add(selfTestSides(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestScripts(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestBuildList(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestSession(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestProblems(report));
	step.add(selfTestDisplay(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestTools(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestPrefabs(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestShortcuts(report));
	step.add(selfTestCompare(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestParticles(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestGraphics(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestLighting(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestSlopes(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestWater(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestPoster(QFileInfo(bmpPath).dir().path(), report));
	step.add(selfTestHistory(report));
	step.add(selfTestSearch(report));
	step.add(selfTestFairness(report));
	step.add(selfTestHelp(report));
	step.add(selfTestSymmetry(report));
	step.add(selfTestTerrain(QFileInfo(bmpPath).dir().path(), report));

	// The window picture of the self test shows the panel of one selected object.
	sel.set(index);
	selectionChanged();
	return step.ok();
}

bool MainWindow::selfTestSelection(QStringList &report)
{
	TestSteps step(report, "selection");
	if (!m_state.map || !m_state.map->map().objects)
		return true;
	MapModel::Map &map = m_state.map->map();
	MapEditorCore::Selection &sel = m_state.map->selection;
	const std::size_t before = map.objects->size();
	// A tree of the map (editor sorting shrubbery) to work with.
	int tree = -1;
	m_pickFilter = 4;
	for (std::size_t i = 0; i < before && tree < 0; ++i)
	{
		const MapModel::MapObject &o = (*map.objects)[i];
		if ((o.flags & 0x36) == 0 && !MapEditorCore::isWaypoint(o) && pickable(static_cast<int>(i)))
		{
			m_pickFilter = 2;
			if (!pickable(static_cast<int>(i)))
				tree = static_cast<int>(i);
			m_pickFilter = 4;
		}
	}
	m_pickFilter = 0;
	step(tree >= 0, QStringLiteral("the map has a tree (%1)").arg(tree >= 0 ? QString::fromStdString((*map.objects)[static_cast<std::size_t>(tree)].templateName) : QString()));
	if (tree < 0)
		return step.ok();
	const std::string treeName = (*map.objects)[static_cast<std::size_t>(tree)].templateName;

	// Pick filter: infantry only lets a click on the tree miss, shrubbery lets it hit.
	sel.clear();
	setPickFilter(2);
	clickObject(tree, false, false);
	step(sel.empty(), QStringLiteral("with \"infantry only\" a click on the tree picks nothing"));
	setPickFilter(4);
	clickObject(tree, false, false);
	step(sel.objects == std::vector<std::int32_t>{tree}, QStringLiteral("with \"shrubbery only\" it picks the tree"));
	selectRect({tree}, false);
	setPickFilter(2);
	selectRect({tree}, false);
	step(sel.empty(), QStringLiteral("a rectangle keeps to the filter too"));
	setPickFilter(0);

	// Similar: every object of the tree's template.
	sel.set(tree);
	selectSimilar();
	std::size_t same = 0;
	for (const MapModel::MapObject &o : *map.objects)
		same += o.templateName == treeName && (o.flags & 0x36) == 0 ? 1 : 0;
	step(sel.objects.size() == same && same >= 1, QStringLiteral("Ctrl+M selects all %1 objects of %2").arg(same).arg(QString::fromStdString(treeName)));

	// Duplicates: a copy of the tree on the same spot, then an object with a team the map lacks.
	selectDuplicates();
	const std::vector<std::int32_t> duplicatesBefore = sel.objects;
	MapModel::MapObject copy = (*map.objects)[static_cast<std::size_t>(tree)];
	copy.properties.erase("uniqueID");
	insertObjects({copy}, "Paste");
	selectDuplicates();
	step(sel.contains(static_cast<std::int32_t>(before)) && sel.objects.size() == duplicatesBefore.size() + 1,
		QStringLiteral("a copy on the same spot is found as duplicate (%1 in the map before)").arg(duplicatesBefore.size()));
	undo();
	MapModel::MapObject stray = copy;
	stray.x += 30.0f;
	stray.properties.set("originalOwner", MapModel::PropertyValue(std::string("teamNobodyHas")));
	insertObjects({stray}, "Paste");
	selectInvalidTeams();
	step(sel.contains(static_cast<std::int32_t>(before)), QStringLiteral("an object of a team the map lacks is found (%1 such objects)").arg(sel.objects.size()));
	undo();
	step(map.objects->size() == before, QStringLiteral("undo all, objects as before"));
	selectNone();
	return step.ok();
}

} // namespace MapEditorApp
