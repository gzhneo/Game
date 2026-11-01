/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowPalette.cpp -- the command palette (Ctrl+Shift+P): every menu command with its
// shortcut, a few settings, and "Go to" for the waypoints, named objects and areas of the map, found
// by typing parts of their names. It replaces the old WorldBuilder's waypoint list and menu search.

#include "App/MainWindow.h"

#include "App/Language.h"
#include "App/Widgets.h"

#include <QAction>
#include <QCoreApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <QVBoxLayout>

namespace MapEditorApp
{

namespace
{

QString plainText(QString text)
{
	return text.remove(QLatin1Char('&'));
}

// Up, down and page keys in the search field move through the list.
class ListKeys : public QObject
{
public:
	explicit ListKeys(QListWidget *list, QObject *parent) : QObject(parent), m_list(list) {}

protected:
	bool eventFilter(QObject *watched, QEvent *event) override
	{
		if (event->type() == QEvent::KeyPress)
		{
			const int key = static_cast<QKeyEvent *>(event)->key();
			if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown)
			{
				QCoreApplication::sendEvent(m_list, event);
				return true;
			}
		}
		return QObject::eventFilter(watched, event);
	}

private:
	QListWidget *m_list;
};

// Every word of the filter somewhere in the text, in any order.
bool matches(const QString &text, const QStringList &words)
{
	for (const QString &w : words)
		if (!text.contains(w, Qt::CaseInsensitive))
			return false;
	return true;
}

} // namespace

std::vector<MainWindow::PaletteEntry> MainWindow::paletteEntries()
{
	std::vector<PaletteEntry> entries;
	// The menus as the bar shows them now (enabled state included).
	if (QMenuBar *bar = menuWidget() ? menuWidget()->findChild<QMenuBar *>() : nullptr)
	{
		std::function<void(QMenu *, const QString &)> walk = [&](QMenu *menu, const QString &path) {
			for (QAction *action : menu->actions())
			{
				if (action->isSeparator() || !action->isVisible())
					continue;
				const QString text = path + QStringLiteral(" \u203a ") + plainText(action->text());
				if (action->menu())
				{
					walk(action->menu(), text);
					continue;
				}
				if (!action->isEnabled())
					continue;
				PaletteEntry e;
				e.text = text;
				e.detail = action->shortcut().toString(QKeySequence::NativeText);
				QPointer<QAction> target(action);
				e.run = [target] {
					if (target)
						target->trigger();
				};
				entries.push_back(std::move(e));
			}
		};
		for (QAction *top : bar->actions())
			if (top->menu())
			{
				// Menus fill their dynamic texts when they open.
				emit top->menu()->aboutToShow();
				walk(top->menu(), plainText(top->text()));
			}
	}
	entries.push_back({m_state.theme == ThemeKind::Dark ? T("Switch to light theme") : T("Switch to dark theme"), QString(), [this] {
		m_state.theme = m_state.theme == ThemeKind::Dark ? ThemeKind::Light : ThemeKind::Dark;
		m_state.save();
		QTimer::singleShot(0, this, [this] { applySettings(); });
	}});
	entries.push_back({m_state.language == Language::German ? QStringLiteral("Switch to English") : QStringLiteral("Auf Deutsch umschalten"), QString(), [this] {
		m_state.language = m_state.language == Language::German ? Language::English : Language::German;
		m_state.save();
		QTimer::singleShot(0, this, [this] { applySettings(); });
	}});
	if (!m_state.map)
		return entries;

	// Go to: waypoints, named objects, areas.
	const MapModel::Map &map = m_state.map->map();
	if (map.objects)
	{
		for (std::size_t i = 0; i < map.objects->size(); ++i)
		{
			const MapModel::MapObject &o = (*map.objects)[i];
			const std::optional<std::string> waypoint = o.properties.getAscii("waypointName");
			const std::optional<std::string> name = o.properties.getAscii("objectName");
			QString text;
			if (waypoint && !waypoint->empty())
				text = T("Go to waypoint %1").arg(utf8(*waypoint));
			else if (name && !name->empty())
				text = T("Go to object %1 (%2)").arg(utf8(*name), utf8(o.templateName));
			else
				continue;
			const auto index = static_cast<std::int32_t>(i);
			entries.push_back({text, QString(), [this, index] {
				if (!m_state.map)
					return;
				m_state.map->selection.set(index);
				selectionChanged();
				centerOnSelection();
			}});
		}
	}
	const std::vector<CameraBookmark> marks = m_state.bookmarks(m_state.map->path);
	for (std::size_t i = 0; i < marks.size(); ++i)
	{
		const int index = static_cast<int>(i);
		entries.push_back({T("Go to bookmark %1").arg(marks[i].name), QString(), [this, index] { goToBookmark(index); }});
	}
	if (map.polygons)
	{
		for (std::size_t i = 0; i < map.polygons->size(); ++i)
		{
			const MapModel::PolygonArea &area = (*map.polygons)[i];
			if (area.name.empty() || area.points.empty())
				continue;
			const int index = static_cast<int>(i);
			entries.push_back({T("Go to area %1").arg(utf8(area.name)), QString(), [this, index] { goToArea(index); }});
		}
	}
	return entries;
}

void MainWindow::goToArea(int index)
{
	if (!m_state.map || !m_state.map->map().polygons || index < 0 || index >= static_cast<int>(m_state.map->map().polygons->size()))
		return;
	const MapModel::PolygonArea &area = (*m_state.map->map().polygons)[static_cast<std::size_t>(index)];
	float x = 0.0f;
	float y = 0.0f;
	for (const auto &p : area.points)
	{
		x += static_cast<float>(p[0]);
		y += static_cast<float>(p[1]);
	}
	const float n = static_cast<float>(std::max<std::size_t>(1, area.points.size()));
	centerOn(x / n, y / n);
}

void MainWindow::showCommands()
{
	std::vector<PaletteEntry> entries = paletteEntries();
	auto *dialog = new QDialog(this, Qt::Popup);
	dialog->setObjectName(QStringLiteral("CommandPalette"));
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->resize(560, 420);
	auto *col = new QVBoxLayout(dialog);
	col->setContentsMargins(8, 8, 8, 8);
	auto *search = new QLineEdit(dialog);
	search->setObjectName(QStringLiteral("CommandSearch"));
	search->setPlaceholderText(T("Type a command, a waypoint, an object or an area"));
	auto *list = new QListWidget(dialog);
	list->setObjectName(QStringLiteral("CommandList"));
	col->addWidget(search);
	col->addWidget(list, 1);
	search->installEventFilter(new ListKeys(list, dialog));

	auto shared = std::make_shared<std::vector<PaletteEntry>>(std::move(entries));
	const auto fill = [list, shared](const QString &filter) {
		list->clear();
		const QStringList words = filter.split(QLatin1Char(' '), Qt::SkipEmptyParts);
		for (std::size_t i = 0; i < shared->size(); ++i)
		{
			const PaletteEntry &e = (*shared)[i];
			if (!matches(e.text, words))
				continue;
			auto *item = new QListWidgetItem(e.detail.isEmpty() ? e.text : e.text + QStringLiteral("    ") + e.detail, list);
			item->setData(Qt::UserRole, static_cast<int>(i));
		}
		if (list->count() > 0)
			list->setCurrentRow(0);
	};
	fill(QString());
	connect(search, &QLineEdit::textChanged, dialog, fill);
	const auto run = [dialog, list, shared] {
		QListWidgetItem *item = list->currentItem();
		if (!item)
			return;
		const std::function<void()> action = (*shared)[static_cast<std::size_t>(item->data(Qt::UserRole).toInt())].run;
		dialog->close();
		// After the popup has gone, so dialogs the command opens get the focus.
		QTimer::singleShot(0, dialog->parent(), action);
	};
	connect(search, &QLineEdit::returnPressed, dialog, run);
	connect(list, &QListWidget::itemActivated, dialog, run);
	const QPoint at = mapToGlobal(QPoint((width() - dialog->width()) / 2, 44));
	dialog->move(at);
	dialog->show();
	search->setFocus();
}

} // namespace MapEditorApp
