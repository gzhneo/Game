/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WindowTree.h -- the tree of windows: dragging an entry hands the drop to the editor, which changes the
// layout and builds the tree again (the tree never moves its own items).

#pragma once

#include <QDropEvent>
#include <QTreeWidget>

#include <functional>

namespace GuiEditorApp
{

class WindowTree : public QTreeWidget
{
public:
	enum class Drop
	{
		Into,		// as the last child of the target
		Before,		// in front of the target, in its parent
		After,		// behind the target, in its parent
		TopLevel,	// as the last top-level window
	};

	explicit WindowTree(QWidget *parent = nullptr) : QTreeWidget(parent)
	{
		setDragDropMode(QAbstractItemView::InternalMove);
		setSelectionMode(QAbstractItemView::ExtendedSelection);
		setDropIndicatorShown(true);
	}

	// The dragged items and where they go; target is null for the top level.
	std::function<void(const QList<QTreeWidgetItem *> &items, QTreeWidgetItem *target, Drop where)> onDrop;

protected:
	void dropEvent(QDropEvent *event) override
	{
		QTreeWidgetItem *target = itemAt(event->position().toPoint());
		Drop where = Drop::TopLevel;
		if (target)
		{
			switch (dropIndicatorPosition())
			{
			case QAbstractItemView::OnItem:
				where = Drop::Into;
				break;
			case QAbstractItemView::AboveItem:
				where = Drop::Before;
				break;
			case QAbstractItemView::BelowItem:
				where = Drop::After;
				break;
			case QAbstractItemView::OnViewport:
				target = nullptr;
				break;
			}
		}
		const QList<QTreeWidgetItem *> items = selectedItems();
		event->setDropAction(Qt::IgnoreAction);
		event->accept();
		if (onDrop && !items.isEmpty())
			onDrop(items, target, where);
	}
};

} // namespace GuiEditorApp
